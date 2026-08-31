/// @file aio/buffer/handle.hpp
/// @brief Move-only RAII lease on a buffer owned by a buffer pool.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <stdexcept>
    #include <utility>
#endif

namespace kmx::aio::buffer
{
    // Forward declaration: a handle names the pool only to befriend it.
    template <typename T, std::size_t Capacity>
    class pool;

    /// @brief Move-only RAII handle leasing a buffer from a pool<T, Capacity>.
    /// @details
    /// Holds exclusive ownership of a leased buffer. On destruction, automatically
    /// returns the buffer to the pool. Non-copyable to enforce single ownership.
    /// Supports move semantics to transfer ownership.
    ///
    /// USAGE:
    /// ```cpp
    /// kmx::aio::buffer::pool<std::vector<std::byte>, 256u> pool;
    /// auto handle = pool.acquire();  // Lease a buffer
    /// handle->resize(1024);          // Modify the buffer
    /// // handle destroyed here → buffer returned to pool
    /// ```
    ///
    /// @tparam T  The type of buffer (must match the pool's element type).
    template <typename T>
    class handle
    {
    public:
        /// @brief Default constructor: constructs an empty (invalid) handle.
        handle() noexcept = default;

        /// @brief Destructor: returns the buffer to the pool (if valid).
        ~handle() noexcept { reset(); }

        /// @brief Non-copyable.
        handle(const handle&) = delete;
        /// @brief Non-copyable.
        handle& operator=(const handle&) = delete;

        /// @brief Move constructor.
        handle(handle&& other) noexcept:
            buffer_(std::exchange(other.buffer_, nullptr)),
            pool_(std::exchange(other.pool_, nullptr)),
            release_fn_(std::exchange(other.release_fn_, nullptr))
        {
        }

        /// @brief Move assignment operator.
        handle& operator=(handle&& other) noexcept
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

        /// @brief Dereference operator: obtains mutable reference to the buffer.
        /// @return Reference to the leased buffer.
        /// @throws std::logic_error if the handle is invalid (already moved or default-constructed).
        [[nodiscard]] T& operator*() noexcept(false)
        {
            validate_or_throw();
            return *buffer_;
        }

        /// @brief Dereference operator (const overload).
        [[nodiscard]] const T& operator*() const noexcept(false)
        {
            validate_or_throw();
            return *buffer_;
        }

        /// @brief Arrow operator: obtains mutable pointer to the buffer for member access.
        /// @return Pointer to the leased buffer.
        /// @throws std::logic_error if the handle is invalid.
        [[nodiscard]] T* operator->() noexcept(false)
        {
            validate_or_throw();
            return buffer_;
        }

        /// @brief Arrow operator (const overload).
        [[nodiscard]] const T* operator->() const noexcept(false)
        {
            validate_or_throw();
            return buffer_;
        }

        /// @brief Obtains raw pointer to the buffer.
        /// @return Pointer to the leased buffer, or nullptr if invalid.
        [[nodiscard]] T* get() noexcept { return buffer_; }
        /// @brief Get raw pointer (const overload).
        [[nodiscard]] const T* get() const noexcept { return buffer_; }

        /// @brief Checks if the handle holds a valid buffer.
        [[nodiscard]] bool valid() const noexcept
        {
            // LCOV_EXCL_BR_LINE: a handle is either fully constructed or fully empty - the three members
            // are set together and cleared together - so the mixed combinations this tests for cannot be
            // built. The check stays because it is the invariant, not an assumption.
            return (buffer_ != nullptr) && (pool_ != nullptr) && (release_fn_ != nullptr); // LCOV_EXCL_BR_LINE
        }

        /// @brief Explicitly releases the buffer back to the pool before destruction.
        /// @details After calling this, the handle becomes invalid. Useful for
        /// deterministic cleanup in performance-critical code.
        void reset() noexcept
        {
            if (valid())
                release_fn_(pool_, buffer_);

            buffer_ = {};
            pool_ = {};
            release_fn_ = {};
        }

    private:
        /// @brief Raw pointer to the leased buffer.
        T* buffer_ {};

        /// @brief Type-erased pointer to the pool (void* for API clarity).
        /// Stores pool* cast to void* to avoid template bloat in handle.
        void* pool_ {};

        /// @brief Type-erased release function: void(*)(void* pool_ptr, T* buf_ptr)
        /// Bound at acquire() time to capture the pool's type and Capacity.
        using release_fn_t = void (*)(void*, T*);
        /// @brief The bound release function, or null for an empty handle.
        release_fn_t release_fn_ {};

        /// @brief Helper to validate the handle.
        void validate_or_throw() const noexcept(false)
        {
            if (!valid())
                throw std::logic_error("buffer::handle: invalid or moved-from handle");
        }

        /// @brief Private constructor used by pool::acquire().
        /// @param buf Raw pointer to the acquired buffer.
        /// @param pool Type-erased pointer to the pool.
        /// @param release_fn Function that will be called on destruction to release the buffer.
        handle(T* buf, void* pool, release_fn_t release_fn) noexcept: buffer_(buf), pool_(pool), release_fn_(release_fn) {}

        // All pool specializations are friends
        template <typename U, std::size_t C>
        friend class pool;
    };
} // namespace kmx::aio::buffer
