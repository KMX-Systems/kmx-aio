/// @file aio/completion/tcp/listener.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/completion/tcp/listener.hpp"

#include "kmx/logger.hpp"
#include <fcntl.h>
#include <netinet/in.h>

namespace kmx::aio::completion::tcp
{
    listener::listener(std::shared_ptr<executor> exec, const ip_address_t ip, const port_t port) noexcept(false): io_base(std::move(exec))
    {
        auto sock_res = file_descriptor::create_socket(ip_family(ip), SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (!sock_res)
            throw std::system_error(sock_res.error(), "socket creation failed");

        fd_ = std::move(sock_res.value());

        const int opt = 1;
        if (auto res = fd_.setsockopt(SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); !res)
            throw std::system_error(res.error(), "setsockopt SO_REUSEADDR failed");

        if (auto res = fd_.setsockopt(SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)); !res)
            throw std::system_error(res.error(), "setsockopt SO_REUSEPORT failed");

        auto addr = make_socket_address(ip, port);
        if (!addr)
            throw std::system_error(addr.error(), "make_socket_address failed");

        if (auto res = fd_.bind(reinterpret_cast<sockaddr*>(&addr->storage), addr->length); !res)
            throw std::system_error(res.error(), "bind failed");
    }

    std::expected<void, std::error_code> listener::listen(const int backlog) noexcept
    {
        return fd_.listen(backlog);
    }

    task<std::expected<file_descriptor, std::error_code>> listener::accept() noexcept(false)
    {
        sockaddr_storage addr {};
        socklen_t addrlen = sizeof(addr);

        auto result = co_await exec_->async_accept(fd_.get(), addr, addrlen);
        if (!result)
            co_return std::unexpected(result.error());

        file_descriptor client_fd(result.value());

        // Set client socket to non-blocking for further io_uring operations
        if (const auto res = client_fd.set_as_non_blocking(); !res)
            co_return std::unexpected(res.error());

        logger::log(logger::level::debug, std::source_location::current(), "completion::tcp::listener accepted fd: {}", client_fd.get());

        co_return client_fd;
    }

} // namespace kmx::aio::completion::tcp
