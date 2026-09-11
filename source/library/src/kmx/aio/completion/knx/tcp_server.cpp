/// @file kmx/aio/completion/knx/tcp_server.cpp
/// @brief The compiled body of the io_uring KNXnet/IP TCP accept loop.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/completion/knx/tcp_server.hpp>

#include <kmx/aio/completion/knx/tcp_transport.hpp>

#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <system_error>
#include <utility>

namespace kmx::aio::completion::knx
{
    tcp_server::tcp_server(executor& exec, kmx::aio::knx::generic_server& server, tcp_server_config config) noexcept:
        exec_(exec),
        server_(server),
        config_(std::move(config))
    {
    }

    tcp_server::~tcp_server() noexcept
    {
        stop();
    }

    expected_void_t tcp_server::listen() noexcept
    {
        if (listener_.is_valid())
            return {};
        auto socket = file_descriptor::create_socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (!socket)
            return std::unexpected(socket.error());
        const int reuse = 1;
        if (const auto set = socket->setsockopt(SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)); !set)
            return set;

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_port = htons(config_.port);
        std::memcpy(&address.sin_addr.s_addr, config_.bind_address.data(), config_.bind_address.size());
        if (const auto bound = socket->bind(reinterpret_cast<const sockaddr*>(&address), sizeof(address)); !bound)
            return bound;
        if (const auto listening = socket->listen(SOMAXCONN); !listening)
            return listening;
        ::socklen_t length = sizeof(address);
        if (::getsockname(socket->get(), reinterpret_cast<sockaddr*>(&address), &length) != 0)
            return std::unexpected(std::error_code(errno, std::generic_category()));

        port_ = ntohs(address.sin_port);
        listener_ = std::move(*socket);
        return {};
    }

    task_returning_expected_void_t tcp_server::serve() noexcept(false)
    {
        if (const auto listening = listen(); !listening)
            co_return listening;
        const auto stop_token = stop_source_.get_token();
        // Shutting the listening socket down completes the accept this loop waits in, which stop() alone would not reach.
        const std::stop_callback wake_on_stop {stop_token,
                                               [this]() noexcept { static_cast<void>(::shutdown(listener_.get(), SHUT_RDWR)); }};
        while (!stop_token.stop_requested())
        {
            sockaddr_storage peer {};
            ::socklen_t peer_length = sizeof(peer);
            const auto accepted = co_await exec_.async_accept(listener_.get(), peer, peer_length);
            if (!accepted && stop_token.stop_requested())
                break;
            // A connection its peer abandoned while it waited to be accepted is simply gone; the next may follow.
            if (!accepted && (accepted.error() == std::errc::connection_aborted))
                continue;
            if (!accepted)
                co_return std::unexpected(accepted.error());

            // Past the limit a connection is closed as it goes out of scope here.
            file_descriptor connection {*accepted};
            if (connections_.load(std::memory_order_relaxed) >= config_.max_connections)
                continue;
            connections_.fetch_add(1u, std::memory_order_relaxed);
            exec_.spawn(serve_connection(std::move(connection), peer, peer_length));
        }
        co_return expected_void_t {};
    }

    task<void> tcp_server::serve_connection(file_descriptor connection, const sockaddr_storage peer,
                                            const ::socklen_t peer_length) noexcept(false)
    {
        const auto descriptor = connection.get();
        // Frames are small and a client waits on each answer, which is the traffic Nagle's algorithm holds back.
        const int one = 1;
        static_cast<void>(::setsockopt(descriptor, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)));
        {
            tcp_transport transport {exec_, std::move(connection), peer, peer_length};
            // Shutting the connection down completes the receive it waits in, which ends its serving loop.
            const std::stop_callback end_on_stop {stop_source_.get_token(),
                                                  [descriptor]() noexcept { static_cast<void>(::shutdown(descriptor, SHUT_RDWR)); }};
            static_cast<void>(co_await server_.serve_connection(transport, config_.on_event));
        }
        connections_.fetch_sub(1u, std::memory_order_relaxed);
    }

    void tcp_server::stop() noexcept
    {
        stop_source_.request_stop();
    }
}
