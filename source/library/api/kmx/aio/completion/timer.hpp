/// @file aio/completion/timer.hpp
/// @brief Completion-model timer using io_uring IORING_OP_TIMEOUT.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <chrono>
    #include <cstdint>
    #include <expected>
    #include <memory>
    #include <system_error>

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
        /// @param exec The completion executor that owns the io_uring instance.
        explicit timer(std::shared_ptr<executor> exec) noexcept: exec_(std::move(exec)) {}

        /// @brief Non-copyable.
        timer(const timer&) = delete;
        /// @brief Non-copyable.
        timer& operator=(const timer&) = delete;

        /// @brief Move constructor.
        timer(timer&&) noexcept = default;
        /// @brief Move assignment.
        timer& operator=(timer&&) noexcept = default;

        ~timer() noexcept = default;

        /// @brief Asynchronously waits for the specified duration.
        /// @param duration The time to wait before the coroutine is resumed.
        /// @return Success or an error if the wait was cancelled.
        /// @throws std::bad_alloc (coroutine frame allocation).
        template <typename Rep, typename Period>
        [[nodiscard]] task<std::expected<void, std::error_code>> wait(const std::chrono::duration<Rep, Period> duration) noexcept(false)
        {
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
            co_return co_await wait_ns(static_cast<std::uint64_t>(ns.count()));
        }

    private:
        /// @brief Internal: submits a timeout SQE with the given nanosecond duration.
        /// @param ns Nanoseconds to wait.
        /// @return A task yielding success or an error.
        [[nodiscard]] task<std::expected<void, std::error_code>> wait_ns(std::uint64_t ns) noexcept(false);

        std::shared_ptr<executor> exec_;
    };

} // namespace kmx::aio::completion
