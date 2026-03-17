/// @file aio/allocator.hpp
/// @brief Thread-local, lockless slab allocator for coroutine frames and I/O payloads.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cassert>
    #include <cstddef>
    #include <cstdlib>
    #include <cstring>
    #include <new>
    #include <vector>
#endif

namespace kmx::aio
{
    /// @brief A fixed-size slab allocator optimized for coroutine frame allocation.
    /// @details Each slab manages a contiguous block of memory partitioned into
    ///          equal-sized slots. Allocation and deallocation are O(1) via a
    ///          free-list embedded in the unused slots themselves. This allocator is
    ///          NOT thread-safe by design; it must be used from a single thread
    ///          (thread-per-core architecture).
    class slab_allocator
    {
    public:
        /// @brief Constructs a slab allocator.
        /// @param slot_size   Size of each allocation slot in bytes. Rounded up to alignment.
        /// @param slot_count  Number of slots in this slab.
        /// @throws std::bad_alloc if the underlying memory cannot be allocated.
        explicit slab_allocator(const std::size_t slot_size, const std::size_t slot_count) noexcept(false):
            slot_size_(align_up(slot_size, alignof(std::max_align_t))),
            slot_count_(slot_count),
            storage_(slot_size_ * slot_count_)
        {
            // Build the embedded free-list by chaining slot headers
            free_head_ = nullptr;
            for (std::size_t i = slot_count_; i > 0u; --i)
            {
                auto* slot = reinterpret_cast<slot_header*>(storage_.data() + ((i - 1u) * slot_size_));
                slot->next = free_head_;
                free_head_ = slot;
            }
        }

        /// @brief Non-copyable.
        slab_allocator(const slab_allocator&) = delete;
        /// @brief Non-copyable.
        slab_allocator& operator=(const slab_allocator&) = delete;

        /// @brief Move constructor.
        slab_allocator(slab_allocator&& other) noexcept:
            slot_size_(other.slot_size_),
            slot_count_(other.slot_count_),
            storage_(std::move(other.storage_)),
            free_head_(other.free_head_),
            allocated_(other.allocated_)
        {
            other.free_head_ = nullptr;
            other.allocated_ = 0u;
        }

        /// @brief Move assignment.
        slab_allocator& operator=(slab_allocator&& other) noexcept
        {
            if (this != &other)
            {
                slot_size_ = other.slot_size_;
                slot_count_ = other.slot_count_;
                storage_ = std::move(other.storage_);
                free_head_ = other.free_head_;
                allocated_ = other.allocated_;
                other.free_head_ = nullptr;
                other.allocated_ = 0u;
            }

            return *this;
        }

        ~slab_allocator() noexcept = default;

        /// @brief Allocates a single slot from the slab.
        /// @return Pointer to the allocated memory, or nullptr if the slab is exhausted.
        [[nodiscard]] void* allocate() noexcept
        {
            if (free_head_ == nullptr)
                return nullptr;

            auto* slot = free_head_;
            free_head_ = slot->next;
            ++allocated_;
            return static_cast<void*>(slot);
        }

        /// @brief Returns a previously allocated slot to the slab.
        /// @param ptr Pointer that was returned by a previous call to allocate().
        /// @warning Behavior is undefined if ptr was not allocated from this slab.
        void deallocate(void* const ptr) noexcept
        {
            if (ptr == nullptr)
                return;

            auto* slot = static_cast<slot_header*>(ptr);
            slot->next = free_head_;
            free_head_ = slot;
            --allocated_;
        }

        /// @brief Returns the fixed slot size (including alignment padding).
        [[nodiscard]] std::size_t slot_size() const noexcept { return slot_size_; }

        /// @brief Returns the total number of slots in this slab.
        [[nodiscard]] std::size_t slot_count() const noexcept { return slot_count_; }

        /// @brief Returns the number of currently allocated slots.
        [[nodiscard]] std::size_t allocated() const noexcept { return allocated_; }

        /// @brief Returns the number of free slots remaining.
        [[nodiscard]] std::size_t available() const noexcept { return slot_count_ - allocated_; }

        /// @brief Checks if a given pointer is managed by this slab allocator.
        /// @param ptr Pointer to check.
        /// @return true if the pointer falls within this slab's memory region.
        [[nodiscard]] bool owns(const void* const ptr) const noexcept
        {
            if (ptr == nullptr)
                return false;

            const auto* p = static_cast<const std::byte*>(ptr);
            const auto* start = storage_.data();
            return p >= start && p < (start + storage_.size());
        }

    private:
        /// @brief Rounds `value` up to the nearest multiple of `alignment`.
        [[nodiscard]] static constexpr std::size_t align_up(const std::size_t value,
                                                             const std::size_t alignment) noexcept
        {
            return (value + alignment - 1u) & ~(alignment - 1u);
        }

        /// @brief Embedded free-list node. Stored in the first bytes of each free slot.
        struct slot_header
        {
            slot_header* next;
        };

        std::size_t slot_size_;
        std::size_t slot_count_;
        std::vector<std::byte> storage_;
        slot_header* free_head_ {};
        std::size_t allocated_ {};
    };

    /// @brief Sets the thread-local instance of the slab allocator.
    void set_thread_allocator(slab_allocator* alloc) noexcept;

    /// @brief Retrieves the thread-local instance of the slab allocator.
    [[nodiscard]] slab_allocator* get_thread_allocator() noexcept;

} // namespace kmx::aio
