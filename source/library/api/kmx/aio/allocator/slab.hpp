/// @file aio/allocator/slab.hpp
/// @brief Thread-local, lockless slab allocator for coroutine frames and I/O payloads.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <atomic>
    #include <cstddef>
    #include <vector>
#endif

namespace kmx::aio::allocator
{
    /// @brief A fixed-size slab allocator optimized for coroutine frame allocation.
    /// @details Each slab manages a contiguous block of memory partitioned into
    ///          equal-sized slots. Allocation and deallocation are O(1) via a
    ///          free-list embedded in the unused slots themselves. This allocator is
    ///          NOT thread-safe by design; it must be used from a single thread
    ///          (thread-per-core architecture).
    class slab
    {
    public:
        /// @brief Constructs a slab allocator.
        /// @param slot_size   Size of each allocation slot in bytes. Rounded up to alignment.
        /// @param slot_count  Number of slots in this slab.
        /// @throws std::bad_alloc if the underlying memory cannot be allocated.
        explicit slab(const std::size_t slot_size, const std::size_t slot_count) noexcept(false);

        /// @brief Non-copyable.
        slab(const slab&) = delete;
        /// @brief Non-copyable.
        slab& operator=(const slab&) = delete;

        /// @brief Move constructor.
        slab(slab&& other) noexcept;

        /// @brief Move assignment.
        slab& operator=(slab&& other) noexcept;

        /// @brief Releases the slab's storage; slots still handed out become dangling.
        ~slab() noexcept = default;

        /// @brief Allocates a single slot from the slab.
        /// @return Pointer to the allocated memory, or nullptr if the slab is exhausted.
        /// @note Owning thread only.
        [[nodiscard]] void* allocate() noexcept;

        /// @brief Returns a previously allocated slot to the slab.
        /// @param ptr Pointer that was returned by a previous call to allocate().
        /// @warning Behavior is undefined if ptr was not allocated from this slab.
        /// @warning Owning thread only. A slot freed from any other thread must go through
        ///          deallocate_remote(), which is what makes the free list safe to share.
        void deallocate(void* const ptr) noexcept;

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
        void deallocate_remote(void* const ptr) noexcept;

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
        [[nodiscard]] bool owns(const void* const ptr) const noexcept;

    private:
        /// @brief Moves everything freed by other threads onto this thread's free list.
        /// @details Taken in one exchange, so a remote deallocation racing with this either lands on
        ///          the list being taken or starts the next one.
        void adopt_remote_free_list() noexcept;

        /// @brief Rounds `value` up to the nearest multiple of `alignment`.
        [[nodiscard]] static constexpr std::size_t align_up(const std::size_t value, const std::size_t alignment) noexcept
        {
            return (value + alignment - 1u) & ~(alignment - 1u);
        }

        /// @brief Embedded free-list node. Stored in the first bytes of each free slot.
        struct slot_header
        {
            /// @brief Next free slot, or null at the end of the list.
            slot_header* next;
        };

        /// @brief Size in bytes of one slot, including the embedded free-list header.
        std::size_t slot_size_;
        /// @brief Total number of slots carved out of @ref storage_.
        std::size_t slot_count_;
        /// @brief The single contiguous block every slot is carved from.
        std::vector<std::byte> storage_;
        /// @brief Head of the owning thread's free list.
        slot_header* free_head_ {};
        /// @brief Slots returned by threads other than the owner, waiting to be adopted.
        std::atomic<slot_header*> remote_free_head_ {};
        /// @brief Number of slots currently handed out.
        std::size_t allocated_ {};
    };
} // namespace kmx::aio::allocator

namespace kmx::aio
{
    /// @brief Sets the thread-local instance of the slab allocator.
    void set_thread_allocator(allocator::slab* alloc) noexcept;

    /// @brief Retrieves the thread-local instance of the slab allocator.
    [[nodiscard]] allocator::slab* get_thread_allocator() noexcept;

} // namespace kmx::aio
