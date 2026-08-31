/// @file aio/completion/timer.hpp
/// @brief Completion-model timer using io_uring IORING_OP_TIMEOUT.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <chrono>
    #include <cstdint>

    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::completion
{
    /// @brief Asynchronous timer using io_uring's native timeout support.
    /// @details Unlike the readiness model (which uses timerfd + epoll), the
    ///          completion timer submits IORING_OP_TIMEOUT directly to io_uring,
    ///          eliminating the need for a separate file descriptor.
    class timer
    {
    public:
        /// @brief Constructs a timer bound to a completion executor.
        /// @param exec The completion executor that owns the io_uring instance. Must outlive this timer.
        explicit timer(executor& exec) noexcept: exec_(exec) {}

        /// @brief Non-copyable.
        timer(const timer&) = delete;
        /// @brief Non-copyable.
        timer& operator=(const timer&) = delete;

        /// @brief Move constructor.
        timer(timer&&) noexcept = default;
        /// @brief Not move-assignable: exec_ is a reference and cannot be reseated. Defaulting this
        ///        made it implicitly deleted anyway.
        timer& operator=(timer&&) = delete;

        /// @brief Destroys the timer; a wait still outstanding is completed by the executor.
        ~timer() noexcept = default;

        /// @brief Asynchronously waits for the specified duration.
        /// @param duration The time to wait before the coroutine is resumed.
        /// @return Success or an error if the wait was cancelled.
        /// @throws std::bad_alloc (coroutine frame allocation).
        template <typename Rep, typename Period>
        [[nodiscard]] task_returning_expected_void_t wait(const std::chrono::duration<Rep, Period> duration) noexcept(false)
        {
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
            co_return co_await wait_ns(static_cast<std::uint64_t>(ns.count()));
        }

    private:
        /// @brief Internal: submits a timeout SQE with the given nanosecond duration.
        /// @param ns Nanoseconds to wait.
        /// @return A task yielding success or an error.
        [[nodiscard]] task_returning_expected_void_t wait_ns(std::uint64_t ns) noexcept(false);

        /// @brief The executor the timeout SQEs are submitted to.
        executor& exec_;
    };

} // namespace kmx::aio::completion
