/// @file src/kmx/aio/readiness/knx/tcp_server.cpp
/// @brief The compiled body of the epoll KNXnet/IP TCP accept loop.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/readiness/knx/tcp_server.hpp>
#ifndef PCH
    #include <kmx/aio/error_code.hpp>
    #include <kmx/aio/readiness/knx/tcp_transport.hpp>

    #include <cerrno>
    #include <chrono>
    #include <cstring>
    #include <system_error>
    #include <utility>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
#endif

namespace kmx::aio::readiness::knx
{
    /// @brief How long one wait for a connection lasts before the listening socket is asked again.
    static constexpr std::uint32_t recheck_interval_ms = 100u;

    [[nodiscard]] static std::uint32_t now_ms() noexcept
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    tcp_server::tcp_server(executor& exec, kmx::aio::knx::generic_server& server, tcp_server_config config) noexcept:
        exec_(exec),
        server_(server),
        config_(std::move(config))
    {
    }

    tcp_server::~tcp_server() noexcept
    {
        stop();
        if (listener_.is_valid())
            exec_.unregister_fd(listener_.get());
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
        if (const auto registered = exec_.register_fd(socket->get()); !registered)
            return registered;

        port_ = ntohs(address.sin_port);
        listener_ = std::move(*socket);
        return {};
    }

    task<file_descriptor::expected_t> tcp_server::accept(sockaddr_storage& peer, ::socklen_t& peer_length) noexcept(false)
    {
        for (;;)
        {
            peer_length = sizeof(peer);
            const auto accepted =
                ::accept4(listener_.get(), reinterpret_cast<sockaddr*>(&peer), &peer_length, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (accepted >= 0)
                co_return file_descriptor {accepted};
            const auto error_number = errno;
            // A connection its peer abandoned while it waited to be accepted is simply gone; the next may follow.
            if ((error_number == EINTR) || (error_number == ECONNABORTED))
                continue;
            if ((error_number != EAGAIN) && (error_number != EWOULDBLOCK))
                co_return std::unexpected(std::error_code(error_number, std::generic_category()));
            // The socket is registered edge-triggered, and a connection that arrived before this wait subscribed raised
            // no edge the wait will see, so the wait is bounded and the socket asked again after it.
            if (co_await exec_.wait_io_until(listener_.get(), event_type::read, now_ms() + recheck_interval_ms) ==
                executor::wait_status::cancelled)
                co_return std::unexpected(to_std_error_code(error_code::operation_cancelled));
        }
    }

    task_returning_expected_void_t tcp_server::serve() noexcept(false)
    {
        if (const auto listening = listen(); !listening)
            co_return listening;
        const auto stop_token = stop_source_.get_token();
        // stop() sets a flag this loop reads between connections; the accept it waits in has to be woken as well.
        const std::stop_callback cancel_on_stop {stop_token, [this]() noexcept { exec_.cancel_io(listener_.get()); }};
        while (!stop_token.stop_requested())
        {
            sockaddr_storage peer {};
            ::socklen_t peer_length {};
            auto accepted = co_await accept(peer, peer_length);
            if (!accepted && stop_token.stop_requested())
                break;
            if (!accepted)
                co_return std::unexpected(accepted.error());
            // Past the limit, or when it cannot be registered, a connection is closed as it goes out of scope here.
            if ((connections_.load(std::memory_order_relaxed) >= config_.max_connections) || !exec_.register_fd(accepted->get()))
                continue;

            connections_.fetch_add(1u, std::memory_order_relaxed);
            exec_.spawn(serve_connection(std::move(*accepted), peer, peer_length));
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
            // stop() has to reach the receive this connection waits in between frames, not only the accept loop.
            const std::stop_callback cancel_on_stop {stop_source_.get_token(),
                                                     [this, descriptor]() noexcept { exec_.cancel_io(descriptor); }};
            static_cast<void>(co_await server_.serve_connection(transport, config_.on_event));
        }
        connections_.fetch_sub(1u, std::memory_order_relaxed);
    }

    void tcp_server::stop() noexcept
    {
        stop_source_.request_stop();
    }
}
