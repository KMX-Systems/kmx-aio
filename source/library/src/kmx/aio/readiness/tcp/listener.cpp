/// @file aio/readiness/tcp/listener.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/readiness/tcp/listener.hpp"

#include "kmx/logger.hpp"
#include <netinet/in.h>

namespace kmx::aio::readiness::tcp
{
    listener::listener(executor& exec, const ip_address_t ip, const port_t port) noexcept(false): io_base(exec)
    {
        auto sock_res = file_descriptor::create_socket(ip_family(ip), SOCK_STREAM, 0);
        if (!sock_res)
            throw std::system_error(sock_res.error());

        fd_ = std::move(sock_res.value());

        const int opt = 1;
        if (auto res = fd_.setsockopt(SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); !res)
            throw std::system_error(res.error());

        auto addr = make_socket_address(ip, port);
        if (!addr)
            throw std::system_error(addr.error());

        if (auto res = fd_.bind(reinterpret_cast<sockaddr*>(&addr->storage), addr->length); !res)
            throw std::system_error(res.error(), "bind failed");
    }

    std::expected<void, std::error_code> listener::listen(const int backlog) noexcept
    {
        if (auto res = fd_.set_as_non_blocking(); !res)
            return res;

        if (auto res = fd_.listen(backlog); !res)
            return res;

        return exec_.register_fd(fd_.get());
    }

    task<std::expected<file_descriptor, std::error_code>> listener::accept() noexcept(false)
    {
        for (::sockaddr_in client_addr {};;)
        {
            client_addr = {};
            ::socklen_t len = sizeof(client_addr);

            auto accept_res = fd_.accept(reinterpret_cast<sockaddr*>(&client_addr), &len);
            if (accept_res)
            {
                auto client_fd = std::move(accept_res.value());

                if (const auto res = client_fd.set_as_non_blocking(); !res)
                    co_return std::unexpected(res.error());

                if (const auto res = exec_.register_fd(client_fd.get()); !res)
                    co_return std::unexpected(res.error());

                logger::log(logger::level::debug, std::source_location::current(), "Accepted connection fd: {}", client_fd.get());
                co_return client_fd;
            }

            if (would_block(accept_res.error()))
            {
                co_await exec_.wait_io(fd_.get(), event_type::read);
                continue;
            }

            co_return std::unexpected(accept_res.error());
        }
    }
} // namespace kmx::aio::readiness::tcp
