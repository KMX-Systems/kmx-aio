/// @file aio/udp/socket.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/udp/socket.hpp"

#include <cerrno>

namespace kmx::aio::udp
{
    socket::create_result socket::create(executor& exec, const int domain, const int type, const int protocol) noexcept
    {
        auto res = descriptor::file::create_socket(domain, type, protocol);
        if (!res)
            return std::unexpected(res.error());

        if (const auto reg = exec.register_fd(res->get()); !reg)
            return std::unexpected(reg.error());

        return socket(exec, std::move(*res));
    }

    socket::result_task socket::recvmsg(struct msghdr* msg, const int flags) noexcept(false)
    {
        if (msg == nullptr)
            co_return std::unexpected(error_from_errno(EINVAL));

        while (true)
        {
            const ssize_t n = ::recvmsg(fd_.get(), msg, flags);
            if (n >= 0)
                co_return static_cast<std::size_t>(n);

            if (would_block(errno))
            {
                co_await exec_.wait_io(fd_.get(), event_type::read);
                continue;
            }

            co_return std::unexpected(error_from_errno());
        }
    }

    socket::result_task socket::sendmsg(const struct msghdr* msg, const int flags) noexcept(false)
    {
        if (msg == nullptr)
            co_return std::unexpected(error_from_errno(EINVAL));

        while (true)
        {
            const ssize_t n = ::sendmsg(fd_.get(), msg, flags);
            if (n >= 0)
                co_return static_cast<std::size_t>(n);

            if (would_block(errno))
            {
                co_await exec_.wait_io(fd_.get(), event_type::write);
                continue;
            }

            co_return std::unexpected(error_from_errno());
        }
    }
} // namespace kmx::aio::udp
