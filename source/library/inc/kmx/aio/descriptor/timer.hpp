/// @file aio/descriptor/timer.hpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <expected>
    #include <sys/timerfd.h>
    #include <system_error>

    #include <kmx/aio/descriptor/file.hpp>
    #include <kmx/aio/executor.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::descriptor
{
    /// @brief RAII wrapper for timerfd with type-safe operations.
    class timer: public file
    {
    public:
        /// @brief Creates an invalid timer object.
        timer() noexcept = default;

        /// @brief Wraps an existing timer file descriptor.
        /// @param fd Timer file descriptor.
        explicit timer(const fd_t fd) noexcept: file(fd) {}

        // Non-copyable
        timer(const timer&) = delete;
        timer& operator=(const timer&) = delete;

        // Move-only
        timer(timer&&) noexcept = default;
        timer& operator=(timer&&) noexcept = default;

        /// @brief Creates a new timer instance.
        /// @param clockid ID of the clock to be used (e.g. CLOCK_MONOTONIC).
        /// @param flags Flags for timerfd_create (e.g. TFD_NONBLOCK | TFD_CLOEXEC).
        /// @return New timer instance or error code.
        [[nodiscard]] static std::expected<timer, std::error_code> create(const int clockid = CLOCK_MONOTONIC,
                                                                          const int flags = TFD_NONBLOCK | TFD_CLOEXEC) noexcept;

        /// @brief Arms (starts) or disarms (stops) the timer.
        /// @param flags Flags for timerfd_settime (e.g. TFD_TIMER_ABSTIME).
        /// @param new_value New timer values.
        /// @param old_value Optional pointer to store the previous timer values.
        /// @return An error_code on failure, or void on success.
        [[nodiscard]] std::expected<void, std::error_code> set_time(const int flags, const ::itimerspec& new_value,
                                                                    ::itimerspec* old_value = nullptr) noexcept;

        /// @brief Asynchronously waits for the timer to expire.
        /// @param exec Executor used to suspend/resume on read readiness.
        /// @return The number of expirations that have occurred.
        /// @throws std::bad_alloc Coroutine frame allocation failure.
        [[nodiscard]] task<std::expected<std::uint64_t, std::error_code>> wait(executor& exec) noexcept(false);
    };
} // namespace kmx::aio::descriptor
