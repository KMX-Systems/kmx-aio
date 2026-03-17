/// @file aio/completion/udp/socket.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/completion/udp/socket.hpp"

namespace kmx::aio::completion::udp
{
    socket::create_result socket::create(std::shared_ptr<executor> exec, const int domain,
                                         const int type, const int protocol) noexcept
    {
        auto res = descriptor::file::create_socket(domain, type, protocol);
        if (!res)
            return std::unexpected(res.error());

        return socket(std::move(exec), std::move(*res));
    }

    socket::result_task socket::recvmsg(::msghdr* msg, const unsigned int flags) noexcept(false)
    {
        co_return co_await exec_->async_recvmsg(fd_.get(), msg, flags);
    }

    socket::result_task socket::sendmsg(const ::msghdr* msg, const unsigned int flags) noexcept(false)
    {
        co_return co_await exec_->async_sendmsg(fd_.get(), msg, flags);
    }

    std::expected<void, std::error_code> socket::bind(const ip_address_t ip, const port_t port) noexcept
    {
        return fd_.bind(ip, port);
    }

} // namespace kmx::aio::completion::udp
