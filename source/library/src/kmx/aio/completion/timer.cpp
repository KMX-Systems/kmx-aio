/// @file aio/completion/timer.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/completion/timer.hpp"

#include <sys/timerfd.h>
#include <unistd.h>

namespace kmx::aio::completion
{
    task<std::expected<void, std::error_code>> timer::wait_ns(const std::uint64_t ns) noexcept(false)
    {
        // For the initial implementation, we fall back to a timerfd approach
        // within the completion model, which still benefits from the unified API.
        // A future optimization would use IORING_OP_TIMEOUT directly.

        // Use timerfd as the interim mechanism
        const int tfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (tfd < 0)
            co_return std::unexpected(std::error_code(errno, std::generic_category()));

        ::itimerspec its {};
        its.it_value.tv_sec = static_cast<time_t>(ns / 1'000'000'000ULL);
        its.it_value.tv_nsec = static_cast<long>(ns % 1'000'000'000ULL);

        if (::timerfd_settime(tfd, 0, &its, nullptr) < 0)
        {
            ::close(tfd);
            co_return std::unexpected(std::error_code(errno, std::generic_category()));
        }

        // Read from timerfd via io_uring async_read
        std::uint64_t expirations {};
        auto result = co_await exec_->async_read(tfd, std::span<char>(reinterpret_cast<char*>(&expirations), sizeof(expirations)));

        ::close(tfd);
        if (!result)
            co_return std::unexpected(result.error());

        co_return std::expected<void, std::error_code> {};
    }

} // namespace kmx::aio::completion
