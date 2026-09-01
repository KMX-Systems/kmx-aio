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
        pool() noexcept
        {
            // Initialize free list as a chain: slots_[0] -> slots_[1] -> ... -> slots_[Capacity-1] -> nullptr
            for (std::size_t i = 0; i + 1u < Capacity; ++i)
                slots_[i].next_free_ = &slots_[i + 1u];

            slots_[Capacity - 1u].next_free_ = nullptr;

            // Set head to first slot
            free_list_head_ = &slots_[0];
        }

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
        /// @throws Whatever T's default constructor throws; the free list is restored first.
        ///
        /// @details
        /// The returned handle manages the buffer's lifetime. When the handle is destroyed,
        /// the buffer is automatically returned to the free list. This ensures deterministic
        /// resource cleanup without explicit deallocation.
        [[nodiscard]] handle<T> acquire() noexcept(false);

        /// @brief Leases a buffer, or reports that none is free.
        /// @return A handle to the leased buffer, or an empty optional when the pool is exhausted.
        /// @throws Whatever T's default constructor throws; the free list is restored first.
        /// @note The primitive of the two: acquire() is this function plus a throw. Exhaustion is what a
        ///       caller on an event loop meets in normal operation, where it is not an error but
        ///       backpressure - the response is to stop taking on new work until a buffer comes back, a
        ///       decision the caller has to make and cannot make from a catch block on a hot path.
        /// @note Exhaustion is the only condition the empty optional reports. A failing constructor still
        ///       throws, because a caller that reads construction failure as backpressure waits for buffers
        ///       to come back that were never taken, and waits forever.
        [[nodiscard]] std::optional<handle<T>> try_acquire() noexcept(std::is_nothrow_default_constructible_v<T>);

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
            alignas(T) std::array<std::byte, sizeof(T)> storage;

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
        void release(T* const ptr) noexcept;

        /// @brief Reinterprets raw pointer as slot pointer (type erasure support).
        /// @param ptr Raw T* pointer (must point to a slot in this pool).
        /// @return Pointer to the containing slot.
        static slot* ptr_to_slot(T* const ptr) noexcept
        {
            // Reinterpret T* as the address of the slot's storage member
            return reinterpret_cast<slot*>(reinterpret_cast<std::byte*>(ptr) - offsetof(slot, storage));
        }

        /// @brief Reinterprets slot pointer as T pointer.
        /// @param s Pointer to a slot.
        /// @return Pointer to the T object within the slot.
        static T* slot_to_ptr(slot* const s) noexcept { return reinterpret_cast<T*>(s->storage.data()); }

        // handle needs access to release() and slot conversion
        friend class handle<T>;
    };

    template <typename T, std::size_t Capacity>
    handle<T> pool<T, Capacity>::acquire() noexcept(false)
    {
        // try_acquire() is the whole operation; the two differ only in how they report an empty pool, and
        // that difference is not worth a second pass over the free list. Checking for a free slot first and
        // then leasing would take the mutex twice and let the pool empty in between.
        auto leased = try_acquire();
        if (!leased)
            throw std::runtime_error("buffer::pool exhausted: all " + std::to_string(Capacity) + " buffers allocated");

        return std::move(*leased);
    }

    template <typename T, std::size_t Capacity>
    std::optional<handle<T>> pool<T, Capacity>::try_acquire() noexcept(std::is_nothrow_default_constructible_v<T>)
    {
        std::lock_guard<std::mutex> lock(free_list_mutex_);

        if (free_list_head_ == nullptr)
            return {};

        // Pop from free list
        slot* acquired_slot = free_list_head_;
        free_list_head_ = acquired_slot->next_free_;
        acquired_slot->next_free_ = nullptr; // Mark as not in free list

        // Construct T in-place. The rollback exists only for a T that can fail: with a nothrow constructor
        // this function is noexcept, where a catch block that rethrows is dead code the compiler warns is a
        // call to terminate.
        T* buffer = slot_to_ptr(acquired_slot);
        if constexpr (std::is_nothrow_default_constructible_v<T>)
        {
            new (buffer) T();
        }
        else
        {
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
        }

        // Increment allocated count
        allocated_count_.fetch_add(1, std::memory_order_release);

        // Create release function (captured in lambda, converted to function pointer)
        auto release_fn = [](void* const pool_ptr, T* const buf_ptr) noexcept
        {
            auto* const owner = static_cast<pool<T, Capacity>*>(pool_ptr);
            owner->release(buf_ptr);
        };

        // Return handle with type-erased pool and release function
        return handle<T>(buffer, this, release_fn);
    }

    template <typename T, std::size_t Capacity>
    void pool<T, Capacity>::release(T* const ptr) noexcept
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
