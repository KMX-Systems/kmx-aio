/// @file aio/descriptor/timer.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/descriptor/timer.hpp"

#include <cerrno>
#include <unistd.h>

namespace kmx::aio::descriptor
{
    std::expected<timer, std::error_code> timer::create(const int clockid, const int flags) noexcept
    {
        const fd_t fd = ::timerfd_create(clockid, flags);
        if (fd < 0)
            return std::unexpected(error_from_errno());

        return timer(fd);
    }

    std::expected<void, std::error_code> timer::set_time(const int flags, const struct itimerspec& new_value,
                                                         struct itimerspec* old_value) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        if (::timerfd_settime(get(), flags, &new_value, old_value) < 0)
            return std::unexpected(error_from_errno());

        return {};
    }

    task<std::expected<std::uint64_t, std::error_code>> timer::wait(executor& exec) noexcept(false)
    {
        while (true)
        {
            std::uint64_t expirations {};
            const ssize_t n = ::read(get(), &expirations, sizeof(expirations));
            if (n == static_cast<ssize_t>(sizeof(expirations)))
                co_return expirations;

            if (n < 0)
            {
                if (would_block(errno))
                {
                    co_await exec.wait_io(get(), event_type::read);
                    continue;
                }

                co_return std::unexpected(error_from_errno());
            }

            // Unlikely to happen with timerfd, but handle partial read
            co_return std::unexpected(error_from_errno(EIO));
        }
    }
} // namespace kmx::aio::descriptor
