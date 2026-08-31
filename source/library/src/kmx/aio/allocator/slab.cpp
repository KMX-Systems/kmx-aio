/// @file aio/allocator/slab.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/allocator/slab.hpp>

#include <kmx/aio/allocator/detail/thread_state.hpp>

namespace kmx::aio::allocator
{
    slab::slab(const std::size_t slot_size, const std::size_t slot_count) noexcept(false):
        slot_size_(align_up(slot_size, alignof(std::max_align_t))),
        slot_count_(slot_count),
        storage_(slot_size_ * slot_count_)
    {
        // Build the embedded free-list by chaining slot headers
        free_head_ = nullptr;
        for (std::size_t i = slot_count_; i > 0u; --i)
        {
            auto* const slot = reinterpret_cast<slot_header*>(storage_.data() + ((i - 1u) * slot_size_));
            slot->next = free_head_;
            free_head_ = slot;
        }
    }

    slab::slab(slab&& other) noexcept:
        slot_size_(other.slot_size_),
        slot_count_(other.slot_count_),
        storage_(std::move(other.storage_)),
        free_head_(other.free_head_),
        remote_free_head_(other.remote_free_head_.exchange(nullptr, std::memory_order_acq_rel)),
        allocated_(other.allocated_)
    {
        other.free_head_ = nullptr;
        other.allocated_ = 0u;
    }

    slab& slab::operator=(slab&& other) noexcept
    {
        if (this != &other)
        {
            slot_size_ = other.slot_size_;
            slot_count_ = other.slot_count_;
            storage_ = std::move(other.storage_);
            free_head_ = other.free_head_;
            remote_free_head_.store(other.remote_free_head_.exchange(nullptr, std::memory_order_acq_rel), std::memory_order_relaxed);
            allocated_ = other.allocated_;
            other.free_head_ = nullptr;
            other.allocated_ = 0u;
        }

        return *this;
    }

    void* slab::allocate() noexcept
    {
        if (free_head_ == nullptr)
            adopt_remote_free_list();

        if (free_head_ == nullptr)
            return nullptr;

        auto* const slot = free_head_;
        free_head_ = slot->next;
        ++allocated_;
        return static_cast<void*>(slot);
    }

    void slab::deallocate(void* const ptr) noexcept
    {
        if (ptr == nullptr)
            return;

        auto* const slot = static_cast<slot_header*>(ptr);
        slot->next = free_head_;
        free_head_ = slot;
        --allocated_;
    }

    void slab::deallocate_remote(void* const ptr) noexcept
    {
        if (ptr == nullptr)
            return;

        auto* const slot = static_cast<slot_header*>(ptr);
        auto* head = remote_free_head_.load(std::memory_order_relaxed);
        do
        {
            slot->next = head;
        } while (!remote_free_head_.compare_exchange_weak(head, slot, std::memory_order_release, std::memory_order_relaxed));
    }

    bool slab::owns(const void* const ptr) const noexcept
    {
        if (ptr == nullptr)
            return false;

        const auto* const p = static_cast<const std::byte*>(ptr);
        const auto* const start = storage_.data();
        return p >= start && p < (start + storage_.size());
    }

    void slab::adopt_remote_free_list() noexcept
    {
        auto* slot = remote_free_head_.exchange(nullptr, std::memory_order_acquire);
        while (slot != nullptr)
        {
            auto* const next = slot->next;
            slot->next = free_head_;
            free_head_ = slot;
            --allocated_;
            slot = next;
        }
    }
} // namespace kmx::aio::allocator

namespace kmx::aio
{
    void set_thread_allocator(allocator::slab* alloc) noexcept
    {
        allocator::detail::current_thread_state().allocator = alloc;
    }

    allocator::slab* get_thread_allocator() noexcept
    {
        return allocator::detail::current_thread_state().allocator;
    }
} // namespace kmx::aio
