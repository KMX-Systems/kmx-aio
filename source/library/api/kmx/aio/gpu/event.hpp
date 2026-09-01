/// @file aio/gpu/event.hpp
/// @brief GPU event awaiter for coroutine suspension on GPU completion.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/gpu/basic_types.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::gpu
{
    /// @brief Forward declaration.
    class stream;

    /// @brief GPU event awaiter for coroutine suspension on GPU completion.
    /// @details Provides a C++20 co_await interface for waiting on GPU events.
    ///          When a coroutine co_awaits an event, it suspends until the GPU work
    ///          that recorded the event has completed.
    class event
    {
    public:
        /// @brief Internal awaiter struct for co_await suspension.
        struct awaiter
        {
            /// @brief The event this awaiter suspends on.
            event& event_;

            /// @brief Tells whether the event has already fired, letting the coroutine continue without suspending.
            /// @return `true` when the event is already signaled.
            bool await_ready() const noexcept;
            /// @brief Registers @p h with the executor so it resumes once the event fires.
            /// @param h The coroutine to resume.
            void await_suspend(coroutine_handle_t h) noexcept;
            /// @brief Completes the await; the event carries no result.
            void await_resume() const noexcept {}
        };

        /// @brief Creates a new GPU event for tracking GPU stream work.
        /// @throws std::system_error if cudaEventCreate fails.
        event() noexcept(false);

        /// @brief Destroys the GPU event.
        ~event() noexcept;

        /// @brief Non-copyable.
        event(const event&) = delete;
        /// @brief Non-copyable.
        event& operator=(const event&) = delete;

        /// @brief Move constructor.
        event(event&& other) noexcept;

        /// @brief Move assignment.
        event& operator=(event&& other) noexcept;

        /// @brief Returns the underlying CUDA event handle.
        [[nodiscard]] event_handle handle() const noexcept { return handle_; }

        /// @brief C++20 awaiter interface for co_await.
        /// @return An awaiter that suspends the coroutine until the event fires.
        [[nodiscard]] awaiter operator co_await() noexcept;

        /// @brief Checks if the event has fired (non-blocking).
        /// @return true if the event is signaled, false otherwise.
        /// @throws std::system_error if cudaEventQuery fails.
        [[nodiscard]] bool is_ready() const noexcept(false);

    private:
        /// @brief The underlying CUDA event handle, or null after a move.
        event_handle handle_ {};

        /// @brief Destroys the CUDA event and clears @ref handle_.
        void destroy() noexcept;

        /// @brief Grants the awaiter access to the raw handle.
        friend struct awaiter;
        /// @brief Lets @ref stream record events directly onto the raw handle.
        friend class stream;
    };

} // namespace kmx::aio::gpu
