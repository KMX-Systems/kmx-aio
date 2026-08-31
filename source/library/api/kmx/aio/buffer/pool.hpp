/// @file aio/buffer/pool.hpp
/// @brief Fixed-capacity buffer pool with RAII-based ownership and zero-copy semantics.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/buffer/handle.hpp>

    #include <array>
    #include <atomic>
    #include <cstddef>
    #include <mutex>
    #include <optional>
    #include <stdexcept>
    #include <string>
    #include <type_traits>
    #include <utility>
#endif

namespace kmx::aio::buffer
{
    /// @brief Fixed-capacity preallocated buffer pool with deterministic ownership.
    /// @details
    /// Provides O(1) acquisition and release of buffers without dynamic allocation.
    /// Buffers are leased via handle<T>, which automatically returns them to
    /// the pool on destruction (RAII semantics). Supports zero-copy I/O workflows.
    ///
    /// THREAD SAFETY:
    ///   Acquisition and release are protected by a mutex. Multiple threads may
    ///   safely acquire/release buffers concurrently. However, individual buffers
    ///   are NOT protected and must not be shared across threads.
    ///
    ///   The mutex is deliberate. A compare-and-swap free list was measured against it here and lost
    ///   twice over: about 2 ns per lease slower with one thread, and three to four times slower with
    ///   four threads hammering one pool, because every thread then retries its swap against the same
    ///   contended head while the mutex simply serializes a critical section a few nanoseconds long.
    ///   See documentation/benchmarking.md.
    ///
    /// MEMORY LAYOUT:
    ///   - Preallocates Capacity buffers at construction (no heap growth).
    ///   - Free list is intrusive (embedded in unused slots, zero malloc overhead).
    ///   - Deterministic latency: no allocation, no garbage collection.
    ///
    /// @tparam T      The type of element to store in each buffer (e.g., std::vector<std::byte>).
    /// @tparam Capacity  Maximum number of buffers in the pool.
    template <typename T, std::size_t Capacity>
    class pool
    {
    public:
        static_assert(Capacity > 0u, "buffer::pool Capacity must be greater than zero");
        static_assert(std::is_default_constructible_v<T>, "buffer::pool<T, Capacity> requires default-constructible T");

        /// @brief Default constructor: initializes all slots and builds free list.
        pool() noexcept;

        /// @brief Destructor: no-op (all memory is stack-allocated).
        ~pool() noexcept = default;

        /// @brief Non-copyable.
        pool(const pool&) = delete;
        /// @brief Non-copyable.
        pool& operator=(const pool&) = delete;

        /// @brief Non-movable (fixed memory location required for intrusive list).
        pool(pool&&) = delete;
        /// @brief Non-movable.
        pool& operator=(pool&&) = delete;

        /// @brief Acquires a buffer from the pool (lease via RAII handle).
        /// @return A handle<T> that holds exclusive ownership until destruction.
        /// @throws std::runtime_error if the pool is exhausted (all Capacity buffers allocated).
        ///
        /// @details
        /// The returned handle manages the buffer's lifetime. When the handle is destroyed,
        /// the buffer is automatically returned to the free list. This ensures deterministic
        /// resource cleanup without explicit deallocation.
        [[nodiscard]] handle<T> acquire() noexcept(false);

        /// @brief Leases a buffer, or reports that none is free.
        /// @return A handle to the leased buffer, or an empty optional when the pool is exhausted.
        /// @note The non-throwing counterpart of acquire(), for callers on an event loop. There, exhaustion is
        ///       not an error but backpressure: the correct response is to stop taking on new work until a
        ///       buffer comes back, which is a decision the caller has to make and cannot make from a catch
        ///       block on a hot path. Throwing also forces every such caller into a try/catch that is easy to
        ///       get wrong - the QUIC read path currently catches, logs and then silently drops the bytes it
        ///       had already read, which on a reliable stream is a protocol violation the peer cannot detect.
        [[nodiscard]] std::optional<handle<T>> try_acquire() noexcept;

        /// @brief Number of buffers currently available (not yet leased).
        [[nodiscard]] std::size_t available() const noexcept { return Capacity - allocated_count_.load(std::memory_order_acquire); }

        /// @brief Number of buffers currently leased (allocated).
        [[nodiscard]] std::size_t allocated() const noexcept { return allocated_count_.load(std::memory_order_acquire); }

        /// @brief Checks if all Capacity slots are currently leased.
        [[nodiscard]] bool is_full() const noexcept { return allocated_count_.load(std::memory_order_acquire) == Capacity; }

        /// @brief Checks if no buffers are currently leased (all slots available).
        [[nodiscard]] bool is_empty() const noexcept { return allocated_count_.load(std::memory_order_acquire) == 0; }

        /// @brief Total capacity (maximum number of buffers).
        [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

    private:
        /// @brief Internal slot structure holding one buffer instance.
        struct slot
        {
            /// @brief Storage for T (uninitialized until acquired).
            std::aligned_storage_t<sizeof(T), alignof(T)> storage;

            /// @brief Intrusive free-list link (valid only when slot is not leased).
            slot* next_free_ {};
        };

        /// @brief Array of preallocated slots.
        std::array<slot, Capacity> slots_;

        /// @brief Head of the free list (intrusive linked list of available slots).
        /// Initially points to slots_[0]; as buffers are acquired, the list shrinks.
        slot* free_list_head_ {};

        /// @brief Count of currently allocated buffers.
        std::atomic<std::size_t> allocated_count_ {};

        /// @brief Mutex protecting free_list_head_ and allocated_count_.
        mutable std::mutex free_list_mutex_;

        /// @brief Releases a buffer back to the free list (called by handle destructor).
        /// @param ptr Pointer to the buffer to release (must be from this pool).
        void release(T* ptr) noexcept;

        /// @brief Reinterprets raw pointer as slot pointer (type erasure support).
        /// @param ptr Raw T* pointer (must point to a slot in this pool).
        /// @return Pointer to the containing slot.
        static slot* ptr_to_slot(T* ptr) noexcept
        {
            // Reinterpret T* as the address of the slot's storage member
            return reinterpret_cast<slot*>(reinterpret_cast<std::byte*>(ptr) - offsetof(slot, storage));
        }

        /// @brief Reinterprets slot pointer as T pointer.
        /// @param s Pointer to a slot.
        /// @return Pointer to the T object within the slot.
        static T* slot_to_ptr(slot* s) noexcept { return reinterpret_cast<T*>(&s->storage); }

        // handle needs access to release() and slot conversion
        friend class handle<T>;
    };

    template <typename T, std::size_t Capacity>
    pool<T, Capacity>::pool() noexcept
    {
        // Initialize free list as a chain: slots_[0] -> slots_[1] -> ... -> slots_[Capacity-1] -> nullptr
        for (std::size_t i = 0; i + 1u < Capacity; ++i)
            slots_[i].next_free_ = &slots_[i + 1u];

        slots_[Capacity - 1u].next_free_ = nullptr;

        // Set head to first slot
        free_list_head_ = &slots_[0];
    }

    template <typename T, std::size_t Capacity>
    std::optional<handle<T>> pool<T, Capacity>::try_acquire() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(free_list_mutex_);
            if (free_list_head_ == nullptr)
                return {};
        }

        // T's constructor may throw, which acquire() propagates; here it is reported the same way exhaustion
        // is, so that a caller on an event loop has exactly one failure path to handle rather than two.
        try
        {
            return acquire();
        }
        catch (...)
        {
            return {};
        }
    }

    template <typename T, std::size_t Capacity>
    handle<T> pool<T, Capacity>::acquire() noexcept(false)
    {
        std::lock_guard<std::mutex> lock(free_list_mutex_);

        if (free_list_head_ == nullptr)
            throw std::runtime_error("buffer::pool exhausted: all " + std::to_string(Capacity) + " buffers allocated");

        // Pop from free list
        slot* acquired_slot = free_list_head_;
        free_list_head_ = acquired_slot->next_free_;
        acquired_slot->next_free_ = nullptr; // Mark as not in free list

        // Construct T in-place
        T* buffer = slot_to_ptr(acquired_slot);
        try
        {
            new (buffer) T();
        }
        catch (...)
        {
            // Restore free-list state if construction fails.
            acquired_slot->next_free_ = free_list_head_;
            free_list_head_ = acquired_slot;
            throw;
        }

        // Increment allocated count
        allocated_count_.fetch_add(1, std::memory_order_release);

        // Create release function (captured in lambda, converted to function pointer)
        auto release_fn = [](void* pool_ptr, T* buf_ptr) noexcept
        {
            auto* const owner = static_cast<pool<T, Capacity>*>(pool_ptr);
            owner->release(buf_ptr);
        };

        // Return handle with type-erased pool and release function
        return handle<T>(buffer, this, release_fn);
    }

    template <typename T, std::size_t Capacity>
    void pool<T, Capacity>::release(T* ptr) noexcept
    {
        // LCOV_EXCL_BR_LINE / LCOV_EXCL_LINE on the return: release() is private and reached only
        // through handle, which checks its pointer before calling. The guard stays because the
        // class is the one thing standing between a double release and a corrupted free list.
        if (ptr == nullptr) // LCOV_EXCL_BR_LINE
            return;         // LCOV_EXCL_LINE

        std::lock_guard<std::mutex> lock(free_list_mutex_);

        slot* released_slot = ptr_to_slot(ptr);

        // Destroy the T object
        ptr->~T();

        // Push back onto free list
        released_slot->next_free_ = free_list_head_;
        free_list_head_ = released_slot;

        // Decrement allocated count
        allocated_count_.fetch_sub(1, std::memory_order_release);
    }
} // namespace kmx::aio::buffer
