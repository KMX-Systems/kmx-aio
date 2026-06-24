/// @file aio/buffer_pool.hpp
/// @brief Fixed-capacity buffer pool with RAII-based ownership and zero-copy semantics.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <atomic>
    #include <cstddef>
    #include <cstdint>
    #include <memory>
    #include <mutex>
    #include <new>
    #include <stdexcept>
    #include <string>
    #include <type_traits>
    #include <utility>
#endif

namespace kmx::aio
{
    // Forward declarations
    template <typename T>
    class buffer_handle;

    /// @brief Fixed-capacity preallocated buffer pool with deterministic ownership.
    /// @details
    /// Provides O(1) acquisition and release of buffers without dynamic allocation.
    /// Buffers are leased via buffer_handle<T>, which automatically returns them to
    /// the pool on destruction (RAII semantics). Supports zero-copy I/O workflows.
    ///
    /// THREAD SAFETY:
    ///   Acquisition and release are protected by a mutex. Multiple threads may
    ///   safely acquire/release buffers concurrently. However, individual buffers
    ///   are NOT protected and must not be shared across threads.
    ///
    /// MEMORY LAYOUT:
    ///   - Preallocates Capacity buffers at construction (no heap growth).
    ///   - Free list is intrusive (embedded in unused slots, zero malloc overhead).
    ///   - Deterministic latency: no allocation, no garbage collection.
    ///
    /// @tparam T      The type of element to store in each buffer (e.g., std::vector<std::byte>).
    /// @tparam Capacity  Maximum number of buffers in the pool.
    template <typename T, std::size_t Capacity>
    class buffer_pool
    {
    public:
        static_assert(Capacity > 0u, "buffer_pool Capacity must be greater than zero");
        static_assert(std::is_default_constructible_v<T>, "buffer_pool<T, Capacity> requires default-constructible T");

        /// @brief Default constructor: initializes all slots and builds free list.
        buffer_pool() noexcept;

        /// @brief Destructor: no-op (all memory is stack-allocated).
        ~buffer_pool() noexcept = default;

        /// @brief Non-copyable.
        buffer_pool(const buffer_pool&) = delete;
        /// @brief Non-copyable.
        buffer_pool& operator=(const buffer_pool&) = delete;

        /// @brief Non-movable (fixed memory location required for intrusive list).
        buffer_pool(buffer_pool&&) = delete;
        /// @brief Non-movable.
        buffer_pool& operator=(buffer_pool&&) = delete;

        /// @brief Acquires a buffer from the pool (lease via RAII handle).
        /// @return A buffer_handle<T> that holds exclusive ownership until destruction.
        /// @throws std::runtime_error if the pool is exhausted (all Capacity buffers allocated).
        ///
        /// @details
        /// The returned handle manages the buffer's lifetime. When the handle is destroyed,
        /// the buffer is automatically returned to the free list. This ensures deterministic
        /// resource cleanup without explicit deallocation.
        [[nodiscard]] buffer_handle<T> acquire() noexcept(false);

        /// @brief Number of buffers currently available (not yet leased).
        [[nodiscard]] std::size_t available() const noexcept;

        /// @brief Number of buffers currently leased (allocated).
        [[nodiscard]] std::size_t allocated() const noexcept;

        /// @brief Checks if all Capacity slots are currently leased.
        [[nodiscard]] bool is_full() const noexcept;

        /// @brief Checks if no buffers are currently leased (all slots available).
        [[nodiscard]] bool is_empty() const noexcept;

        /// @brief Total capacity (maximum number of buffers).
        [[nodiscard]] static constexpr std::size_t capacity() noexcept;

    private:
        /// @brief Internal slot structure holding one buffer instance.
        struct slot
        {
            /// @brief Storage for T (uninitialized until acquired).
            std::aligned_storage_t<sizeof(T), alignof(T)> storage;

            /// @brief Intrusive free-list link (valid only when slot is not leased).
            slot* next_free_ = nullptr;
        };

        /// @brief Array of preallocated slots.
        std::array<slot, Capacity> slots_;

        /// @brief Head of the free list (intrusive linked list of available slots).
        /// Initially points to slots_[0]; as buffers are acquired, the list shrinks.
        slot* free_list_head_ = nullptr;

        /// @brief Count of currently allocated buffers.
        std::atomic<std::size_t> allocated_count_{};

        /// @brief Mutex protecting free_list_head_ and allocated_count_.
        mutable std::mutex free_list_mutex_;

        /// @brief Releases a buffer back to the free list (called by buffer_handle destructor).
        /// @param ptr Pointer to the buffer to release (must be from this pool).
        void release(T* ptr) noexcept;

        /// @brief Reinterprets raw pointer as slot pointer (type erasure support).
        /// @param ptr Raw T* pointer (must point to a slot in this pool).
        /// @return Pointer to the containing slot.
        static slot* ptr_to_slot(T* ptr) noexcept;

        /// @brief Reinterprets slot pointer as T pointer.
        /// @param s Pointer to a slot.
        /// @return Pointer to the T object within the slot.
        static T* slot_to_ptr(slot* s) noexcept;

        // buffer_handle needs access to release() and slot conversion
        friend class buffer_handle<T>;
    };

    /// @brief Move-only RAII handle leasing a buffer from a buffer_pool<T, Capacity>.
    /// @details
    /// Holds exclusive ownership of a leased buffer. On destruction, automatically
    /// returns the buffer to the pool. Non-copyable to enforce single ownership.
    /// Supports move semantics to transfer ownership.
    ///
    /// USAGE:
    /// ```cpp
    /// kmx::aio::buffer_pool<std::vector<std::byte>, 256> pool;
    /// auto handle = pool.acquire();  // Lease a buffer
    /// handle->resize(1024);          // Modify the buffer
    /// // handle destroyed here → buffer returned to pool
    /// ```
    ///
    /// @tparam T  The type of buffer (must match the pool's element type).
    template <typename T>
    class buffer_handle
    {
    public:
        /// @brief Default constructor: constructs an empty (invalid) handle.
        buffer_handle() noexcept = default;

        /// @brief Destructor: returns the buffer to the pool (if valid).
        ~buffer_handle() noexcept;

        /// @brief Non-copyable.
        buffer_handle(const buffer_handle&) = delete;
        /// @brief Non-copyable.
        buffer_handle& operator=(const buffer_handle&) = delete;

        /// @brief Move constructor.
        buffer_handle(buffer_handle&& other) noexcept;

        /// @brief Move assignment operator.
        buffer_handle& operator=(buffer_handle&& other) noexcept;

        /// @brief Dereference operator: obtains mutable reference to the buffer.
        /// @return Reference to the leased buffer.
        /// @throws std::logic_error if the handle is invalid (already moved or default-constructed).
        [[nodiscard]] T& operator*() noexcept(false);
        /// @brief Dereference operator (const overload).
        [[nodiscard]] const T& operator*() const noexcept(false);

        /// @brief Arrow operator: obtains mutable pointer to the buffer for member access.
        /// @return Pointer to the leased buffer.
        /// @throws std::logic_error if the handle is invalid.
        [[nodiscard]] T* operator->() noexcept(false);
        /// @brief Arrow operator (const overload).
        [[nodiscard]] const T* operator->() const noexcept(false);

        /// @brief Obtains raw pointer to the buffer.
        /// @return Pointer to the leased buffer, or nullptr if invalid.
        [[nodiscard]] T* get() noexcept;
        /// @brief Get raw pointer (const overload).
        [[nodiscard]] const T* get() const noexcept;

        /// @brief Checks if the handle holds a valid buffer.
        [[nodiscard]] bool valid() const noexcept;

        /// @brief Explicitly releases the buffer back to the pool before destruction.
        /// @details After calling this, the handle becomes invalid. Useful for
        /// deterministic cleanup in performance-critical code.
        void reset() noexcept;

    private:
        /// @brief Raw pointer to the leased buffer.
        T* buffer_ = nullptr;

        /// @brief Type-erased pointer to the buffer_pool (void* for API clarity).
        /// Stores pool* cast to void* to avoid template bloat in buffer_handle.
        void* pool_ = nullptr;

        /// @brief Type-erased release function: void(*)(void* pool_ptr, T* buf_ptr)
        /// Bound at acquire() time to capture the pool's type and Capacity.
        using release_fn_t = void (*)(void*, T*);
        release_fn_t release_fn_ = nullptr;

        /// @brief Helper to validate the handle.
        void validate_or_throw() const noexcept(false);

        /// @brief Private constructor used by buffer_pool::acquire().
        /// @param buf Raw pointer to the acquired buffer.
        /// @param pool Type-erased pointer to the pool.
        /// @param release_fn Function that will be called on destruction to release the buffer.
        buffer_handle(T* buf, void* pool, release_fn_t release_fn) noexcept;

        // All buffer_pool specializations are friends
        template <typename U, std::size_t C>
        friend class buffer_pool;
    };

    // ===================================================================
    // Inline implementations
    // ===================================================================

    template <typename T, std::size_t Capacity>
    constexpr std::size_t buffer_pool<T, Capacity>::capacity() noexcept
    {
        return Capacity;
    }

    template <typename T, std::size_t Capacity>
    buffer_pool<T, Capacity>::buffer_pool() noexcept
    {
        // Initialize free list as a chain: slots_[0] -> slots_[1] -> ... -> slots_[Capacity-1] -> nullptr
        for (std::size_t i = 0; i + 1u < Capacity; ++i)
        {
            slots_[i].next_free_ = &slots_[i + 1];
        }
        slots_[Capacity - 1].next_free_ = nullptr;

        // Set head to first slot
        free_list_head_ = &slots_[0];
    }

    template <typename T, std::size_t Capacity>
    buffer_handle<T> buffer_pool<T, Capacity>::acquire() noexcept(false)
    {
        std::lock_guard<std::mutex> lock(free_list_mutex_);

        if (free_list_head_ == nullptr)
        {
            throw std::runtime_error("buffer_pool exhausted: all " + std::to_string(Capacity) + " buffers allocated");
        }

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
            auto* pool = static_cast<buffer_pool<T, Capacity>*>(pool_ptr);
            pool->release(buf_ptr);
        };

        // Return handle with type-erased pool and release function
        return buffer_handle<T>(buffer, this, release_fn);
    }

    template <typename T, std::size_t Capacity>
    std::size_t buffer_pool<T, Capacity>::available() const noexcept
    {
        return Capacity - allocated_count_.load(std::memory_order_acquire);
    }

    template <typename T, std::size_t Capacity>
    std::size_t buffer_pool<T, Capacity>::allocated() const noexcept
    {
        return allocated_count_.load(std::memory_order_acquire);
    }

    template <typename T, std::size_t Capacity>
    bool buffer_pool<T, Capacity>::is_full() const noexcept
    {
        return allocated_count_.load(std::memory_order_acquire) == Capacity;
    }

    template <typename T, std::size_t Capacity>
    bool buffer_pool<T, Capacity>::is_empty() const noexcept
    {
        return allocated_count_.load(std::memory_order_acquire) == 0;
    }

    template <typename T, std::size_t Capacity>
    void buffer_pool<T, Capacity>::release(T* ptr) noexcept
    {
        if (ptr == nullptr)
            return;

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

    template <typename T, std::size_t Capacity>
    typename buffer_pool<T, Capacity>::slot* buffer_pool<T, Capacity>::ptr_to_slot(T* ptr) noexcept
    {
        // Reinterpret T* as the address of the slot's storage member
        return reinterpret_cast<slot*>(reinterpret_cast<std::byte*>(ptr) - offsetof(slot, storage));
    }

    template <typename T, std::size_t Capacity>
    T* buffer_pool<T, Capacity>::slot_to_ptr(slot* s) noexcept
    {
        return reinterpret_cast<T*>(&s->storage);
    }

    // buffer_handle implementations
    template <typename T>
    buffer_handle<T>::buffer_handle(T* buf, void* pool, release_fn_t release_fn) noexcept:
        buffer_(buf),
        pool_(pool),
        release_fn_(release_fn)
    {
    }

    template <typename T>
    buffer_handle<T>::~buffer_handle() noexcept
    {
        reset();
    }

    template <typename T>
    buffer_handle<T>::buffer_handle(buffer_handle&& other) noexcept:
        buffer_(std::exchange(other.buffer_, nullptr)),
        pool_(std::exchange(other.pool_, nullptr)),
        release_fn_(std::exchange(other.release_fn_, nullptr))
    {
    }

    template <typename T>
    buffer_handle<T>& buffer_handle<T>::operator=(buffer_handle&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            buffer_ = std::exchange(other.buffer_, nullptr);
            pool_ = std::exchange(other.pool_, nullptr);
            release_fn_ = std::exchange(other.release_fn_, nullptr);
        }
        return *this;
    }

    template <typename T>
    T& buffer_handle<T>::operator*() noexcept(false)
    {
        validate_or_throw();
        return *buffer_;
    }

    template <typename T>
    const T& buffer_handle<T>::operator*() const noexcept(false)
    {
        validate_or_throw();
        return *buffer_;
    }

    template <typename T>
    T* buffer_handle<T>::operator->() noexcept(false)
    {
        validate_or_throw();
        return buffer_;
    }

    template <typename T>
    const T* buffer_handle<T>::operator->() const noexcept(false)
    {
        validate_or_throw();
        return buffer_;
    }

    template <typename T>
    T* buffer_handle<T>::get() noexcept
    {
        return buffer_;
    }

    template <typename T>
    const T* buffer_handle<T>::get() const noexcept
    {
        return buffer_;
    }

    template <typename T>
    bool buffer_handle<T>::valid() const noexcept
    {
        return buffer_ != nullptr && pool_ != nullptr && release_fn_ != nullptr;
    }

    template <typename T>
    void buffer_handle<T>::reset() noexcept
    {
        if (valid())
        {
            release_fn_(pool_, buffer_);
        }
        buffer_ = nullptr;
        pool_ = nullptr;
        release_fn_ = nullptr;
    }

    template <typename T>
    void buffer_handle<T>::validate_or_throw() const noexcept(false)
    {
        if (!valid())
        {
            throw std::logic_error("buffer_handle: invalid or moved-from handle");
        }
    }

} // namespace kmx::aio
