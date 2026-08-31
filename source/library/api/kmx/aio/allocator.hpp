/// @file aio/allocator.hpp
/// @brief Thread-local, lockless slab allocator for coroutine frames and I/O payloads.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <atomic>
    #include <cassert>
    #include <cstddef>
    #include <cstdint>
    #include <cstdlib>
    #include <cstring>
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
                auto* const slot = reinterpret_cast<slot_header*>(storage_.data() + ((i - 1u) * slot_size_));
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
            remote_free_head_(other.remote_free_head_.exchange(nullptr, std::memory_order_acq_rel)),
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
                remote_free_head_.store(other.remote_free_head_.exchange(nullptr, std::memory_order_acq_rel), std::memory_order_relaxed);
                allocated_ = other.allocated_;
                other.free_head_ = nullptr;
                other.allocated_ = 0u;
            }

            return *this;
        }

        ~slab_allocator() noexcept = default;

        /// @brief Allocates a single slot from the slab.
        /// @return Pointer to the allocated memory, or nullptr if the slab is exhausted.
        /// @note Owning thread only.
        [[nodiscard]] void* allocate() noexcept
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

        /// @brief Returns a previously allocated slot to the slab.
        /// @param ptr Pointer that was returned by a previous call to allocate().
        /// @warning Behavior is undefined if ptr was not allocated from this slab.
        /// @warning Owning thread only. A slot freed from any other thread must go through
        ///          deallocate_remote(), which is what makes the free list safe to share.
        void deallocate(void* const ptr) noexcept
        {
            if (ptr == nullptr)
                return;

            auto* const slot = static_cast<slot_header*>(ptr);
            slot->next = free_head_;
            free_head_ = slot;
            --allocated_;
        }

        /// @brief Returns a slot to the slab from a thread that does not own it.
        /// @param ptr Pointer that was returned by a previous call to allocate().
        /// @details Pushed onto a separate lock-free list that the owning thread adopts the next time
        ///          its own list runs dry. The owner's free list is never touched from here, so the
        ///          single-threaded fast path stays exactly as fast as it was.
        /// @note This is what a coroutine frame needs: a frame is allocated wherever the coroutine was
        ///       created and destroyed wherever it last ran, and for anything spanning an executor
        ///       boundary those are different threads. Sending such a frame to ::operator delete - or
        ///       to another thread's free list - corrupts the heap.
        /// @warning Behavior is undefined if ptr was not allocated from this slab.
        void deallocate_remote(void* const ptr) noexcept
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

        /// @brief Returns the fixed slot size (including alignment padding).
        [[nodiscard]] std::size_t slot_size() const noexcept { return slot_size_; }

        /// @brief Returns the total number of slots in this slab.
        [[nodiscard]] std::size_t slot_count() const noexcept { return slot_count_; }

        /// @brief Returns the number of currently allocated slots.
        /// @note Slots freed by another thread are counted as still allocated until the owning thread
        ///       adopts them, which it does the next time it needs a slot.
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

            const auto* const p = static_cast<const std::byte*>(ptr);
            const auto* const start = storage_.data();
            return p >= start && p < (start + storage_.size());
        }

    private:
        /// @brief Moves everything freed by other threads onto this thread's free list.
        /// @details Taken in one exchange, so a remote deallocation racing with this either lands on
        ///          the list being taken or starts the next one.
        void adopt_remote_free_list() noexcept
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

        /// @brief Rounds `value` up to the nearest multiple of `alignment`.
        [[nodiscard]] static constexpr std::size_t align_up(const std::size_t value, const std::size_t alignment) noexcept
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
        /// @brief Slots returned by threads other than the owner, waiting to be adopted.
        std::atomic<slot_header*> remote_free_head_ {};
        std::size_t allocated_ {};
    };

    namespace detail
    {
        /// @brief Everything the coroutine-frame allocator keeps per thread.
        /// @details One thread-local block holds both the slab this thread allocates frames from and
        ///          the counters saying where those frames came from, so the allocation path performs a
        ///          single thread-local lookup rather than one per item.
        /// @note The counters are atomic only so that the statistics may be read from another thread
        ///       without a data race. They are written exclusively by their owning thread, and with a
        ///       plain relaxed load-add-store rather than a read-modify-write: a locked increment of a
        ///       process-wide counter costs more than the slab allocation it is counting, and puts
        ///       every executor thread on the same cache line to pay for it.
        /// @note A block is never destroyed. It is small, and outliving its thread is what lets the
        ///       process-wide totals stay correct - and readable - after that thread has exited,
        ///       without any teardown ordering to get wrong on a path that runs for every coroutine
        ///       frame. One block is kept per thread that has allocated a frame.
        struct thread_state
        {
            /// @brief Counts one coroutine frame served from the slab.
            void count_slab_allocation() noexcept
            {
                slab_allocations.store(slab_allocations.load(std::memory_order_relaxed) + 1u, std::memory_order_relaxed);
            }

            /// @brief Counts one coroutine frame served from the heap.
            void count_heap_allocation() noexcept
            {
                heap_allocations.store(heap_allocations.load(std::memory_order_relaxed) + 1u, std::memory_order_relaxed);
            }

            /// @brief The slab this thread allocates coroutine frames from, if one was installed.
            slab_allocator* allocator {};
            /// @brief Frames this thread took from its slab.
            std::atomic_uint64_t slab_allocations {};
            /// @brief Frames this thread took from the heap.
            std::atomic_uint64_t heap_allocations {};
            /// @brief Next block in the process-wide registry.
            thread_state* next {};
        };

        /// @brief Returns the calling thread's block, creating and registering it on first use.
        [[nodiscard]] thread_state& current_thread_state() noexcept;

        /// @brief Which of the per-thread counters a process-wide total refers to.
        enum class counter_kind : std::uint8_t
        {
            slab, ///< Frames served from a thread's slab.
            heap  ///< Frames served from the heap.
        };
    } // namespace detail

    /// @brief A process-wide allocation total, summed from the per-thread counters when read.
    /// @details Reads like the atomic counter it replaces - `load()` with an optional memory order -
    ///          but there is no single location being read: the count is spread across the threads that
    ///          did the allocating, which is what keeps the allocation path free of shared writes.
    class allocation_counter
    {
    public:
        /// @brief Binds the total to one of the per-thread counters.
        /// @param kind Which counter this total sums.
        explicit constexpr allocation_counter(const detail::counter_kind kind) noexcept: kind_(kind) {}

        /// @brief Sums the counter across every thread that has allocated a coroutine frame.
        /// @param order Ignored; accepted so this reads like the atomic it replaces.
        /// @return The process-wide total.
        [[nodiscard]] std::uint64_t load(std::memory_order order = std::memory_order_relaxed) const noexcept;

    private:
        /// @brief Which per-thread counter is summed.
        detail::counter_kind kind_;
    };

    /// @brief Sets the thread-local instance of the slab allocator.
    void set_thread_allocator(slab_allocator* alloc) noexcept;

    /// @brief Retrieves the thread-local instance of the slab allocator.
    [[nodiscard]] slab_allocator* get_thread_allocator() noexcept;

    /// @brief Process-wide counters describing coroutine-frame allocation routing.
    /// @details Each total is summed from the per-thread counters at the moment it is read, so a
    ///          reference kept from an earlier call keeps reporting current values.
    struct allocator_statistics
    {
        allocation_counter slab_allocations {detail::counter_kind::slab};
        allocation_counter heap_allocations {detail::counter_kind::heap};

        /// @brief Zeroes the totals, including the counts every thread is holding.
        void reset() noexcept;
    };

    /// @brief Returns the process-wide allocator statistics.
    [[nodiscard]] allocator_statistics& get_allocator_statistics() noexcept;

} // namespace kmx::aio
