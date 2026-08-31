/// @file aio/async_mutex.hpp
/// @brief A mutex a coroutine can hold across a suspension.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// A std::mutex cannot guard a region that contains a co_await. The coroutine may resume on a
/// different thread than the one that locked, and unlocking a std::mutex from a thread that does not
/// own it is undefined; and even where it happens to work, a coroutine parked on a socket while
/// holding a std::mutex blocks the worker thread that carries it rather than yielding it. This mutex
/// suspends the coroutine instead of blocking the thread, and hands ownership to the next waiter when
/// the holder releases - so a region such as "read the socket, then feed what arrived into the TLS
/// read BIO" can be made atomic even though a suspension sits in the middle of it.
#pragma once
#ifndef PCH
    #include <coroutine>
    #include <deque>
    #include <mutex>
    #include <utility>
#endif

namespace kmx::aio
{
    using coroutine_handle_t = std::coroutine_handle<>;

    /// @brief A mutex acquired by co_await rather than by blocking.
    /// @details Ownership is handed straight from the releasing holder to the first waiter in line, so
    ///          waiters are served in the order they arrived and none of them is woken only to find the
    ///          mutex taken again.
    /// @note Not recursive: a coroutine that already holds it and awaits it again deadlocks.
    /// @warning The releasing holder resumes the next waiter on its own thread, inline. Everything the
    ///          resumed coroutine does up to its next suspension therefore runs before unlock()
    ///          returns.
    class async_mutex
    {
    public:
        /// @brief Ownership of an async_mutex, released when it goes out of scope.
        class guard
        {
        public:
            /// @brief Constructs a guard owning nothing.
            guard() noexcept = default;

            /// @brief Adopts ownership already acquired from @p owner.
            /// @param owner The mutex this guard releases.
            explicit guard(async_mutex& owner) noexcept: owner_(&owner) {}

            /// @brief Non-copyable: ownership is held once.
            guard(const guard&) = delete;
            /// @brief Non-copyable.
            guard& operator=(const guard&) = delete;

            /// @brief Takes over ownership, leaving @p other holding nothing.
            /// @param other The guard to move from.
            guard(guard&& other) noexcept: owner_(std::exchange(other.owner_, nullptr)) {}

            /// @brief Releases what this guard holds and takes over @p other.
            /// @param other The guard to move from.
            /// @return This guard.
            guard& operator=(guard&& other) noexcept
            {
                if (this != &other)
                {
                    release();
                    owner_ = std::exchange(other.owner_, nullptr);
                }

                return *this;
            }

            /// @brief Releases the mutex, if this guard still owns it.
            ~guard() noexcept { release(); }

            /// @brief Whether this guard owns a mutex.
            [[nodiscard]] bool owns_lock() const noexcept { return owner_ != nullptr; }

            /// @brief Releases the mutex early, before the guard goes out of scope.
            void release() noexcept
            {
                if (owner_ != nullptr)
                    std::exchange(owner_, nullptr)->unlock();
            }

        private:
            /// @brief The mutex owned, or nullptr for a guard that owns nothing.
            async_mutex* owner_ {};
        };

        /// @brief The awaitable returned by lock().
        class awaiter
        {
        public:
            /// @brief Binds the awaiter to the mutex it acquires.
            /// @param owner The mutex to acquire.
            explicit awaiter(async_mutex& owner) noexcept: owner_(owner) {}

            /// @brief Takes the mutex without suspending when it is free.
            /// @return True when ownership was taken here, false to go on to await_suspend().
            [[nodiscard]] bool await_ready() noexcept { return owner_.try_lock(); }

            /// @brief Queues the coroutine behind the current holder.
            /// @param handle The coroutine to resume once ownership passes to it.
            /// @return True to stay suspended, false when the mutex fell free in the meantime.
            /// @throws std::bad_alloc if the waiter queue cannot grow.
            [[nodiscard]] bool await_suspend(coroutine_handle_t handle) noexcept(false) { return owner_.enqueue(handle); }

            /// @brief Hands the caller the ownership it now holds.
            /// @return A guard that releases the mutex.
            [[nodiscard]] guard await_resume() noexcept { return guard {owner_}; }

        private:
            /// @brief The mutex being acquired.
            async_mutex& owner_;
        };

        /// @brief Constructs an unlocked mutex.
        async_mutex() noexcept = default;

        /// @brief Non-copyable.
        async_mutex(const async_mutex&) = delete;
        /// @brief Non-copyable.
        async_mutex& operator=(const async_mutex&) = delete;
        /// @brief Non-movable: waiters hold a reference to this object.
        async_mutex(async_mutex&&) = delete;
        /// @brief Non-movable.
        async_mutex& operator=(async_mutex&&) = delete;

        /// @brief Destroys the mutex.
        /// @warning Destroying one that still has waiters leaks their coroutines: nothing resumes them.
        ~async_mutex() noexcept = default;

        /// @brief Acquires the mutex, suspending the caller if it is held.
        /// @return An awaitable yielding a guard that releases the mutex.
        [[nodiscard]] awaiter lock() noexcept { return awaiter {*this}; }

        /// @brief Takes the mutex if it is free, without suspending.
        /// @return True when ownership was taken.
        [[nodiscard]] bool try_lock() noexcept;

        /// @brief Releases the mutex, passing ownership to the first waiter if there is one.
        /// @note Resumes that waiter inline; see the class warning.
        void unlock() noexcept;

    private:
        /// @brief Queues @p handle, unless the mutex fell free first and was taken for it.
        /// @param handle The coroutine to queue.
        /// @return True when the coroutine was queued and must stay suspended.
        /// @throws std::bad_alloc if the waiter queue cannot grow.
        [[nodiscard]] bool enqueue(coroutine_handle_t handle) noexcept(false);

        /// @brief Guards @c held_ and @c waiters_.
        std::mutex state_mutex_;
        /// @brief Whether the mutex is owned by anybody.
        bool held_ {};
        /// @brief Coroutines waiting for ownership, in arrival order.
        std::deque<coroutine_handle_t> waiters_;
    };

} // namespace kmx::aio
