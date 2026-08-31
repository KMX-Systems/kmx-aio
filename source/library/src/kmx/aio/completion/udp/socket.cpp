/// @file aio/completion/udp/socket.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/completion/udp/socket.hpp>

namespace kmx::aio::completion::udp
{
    socket::expected_t socket::create(executor& exec, const int domain, const int type, const int protocol) noexcept
    {
        auto res = file_descriptor::create_socket(domain, type, protocol);
        if (!res)
            return std::unexpected(res.error());

        return socket(exec, std::move(*res));
    }

    task_returning_expected_size_t socket::recvmsg(::msghdr* msg, const unsigned flags) noexcept(false)
    {
        co_return co_await exec_.async_recvmsg(fd_.get(), msg, flags);
    }

    task_returning_expected_size_t socket::sendmsg(const ::msghdr* msg, const unsigned flags) noexcept(false)
    {
        co_return co_await exec_.async_sendmsg(fd_.get(), msg, flags);
    }

    expected_void_t socket::bind(const ip_address_t ip, const port_t port) noexcept
    {
        return fd_.bind(ip, port);
    }

} // namespace kmx::aio::completion::udp
