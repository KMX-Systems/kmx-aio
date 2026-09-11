/// @file aio/readiness/knx/tcp_server.hpp
/// @brief Readiness accept loop for KNXnet/IP over TCP: one serving task per connection.
/// @details
/// A @ref kmx::aio::knx::generic_server serves each TCP connection through
/// @ref kmx::aio::knx::generic_server::serve_connection. This loop listens, accepts, wraps every connection in a
/// @ref kmx::aio::readiness::knx::tcp_transport and spawns the task that serves it - the shape of the Modbus TCP
/// server. Past @ref kmx::aio::readiness::knx::tcp_server_config::max_connections a connection is closed as soon as it
/// is accepted.
///
/// The accept is this loop's own rather than @ref kmx::aio::readiness::tcp::listener::accept, which parks in an
/// unbounded wait: the listening socket is registered edge-triggered, and a connection that arrives between a refused
/// accept and that wait raises no edge the wait can see. Here the wait is bounded and the socket asked again after it.
/// @note The server and this loop must outlive the executor's run, since every connection task refers to both.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_READINESS) && defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <atomic>
        #include <cstddef>
        #include <cstdint>
        #include <stop_token>
        #include <sys/socket.h>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/file_descriptor.hpp>
    #include <kmx/aio/ipv4.hpp>
    #include <kmx/aio/knx/server.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/task.hpp>

namespace kmx::aio::readiness::knx
{
    /// @brief Where a KNXnet/IP TCP server listens, how many connections it holds, and where their frames go.
    struct tcp_server_config
    {
        /// @brief The IPv4 address to listen on; every address by default.
        ipv4::storage_t bind_address = ipv4::any;
        /// @brief The TCP port to listen on; zero lets the kernel choose, and @ref tcp_server::port reports its choice.
        port_t port = 3671u;
        /// @brief How many connections may be served at once.
        std::size_t max_connections = 16u;
        /// @brief What each tunnelled frame is handed to; frames are dropped when this is empty.
        kmx::aio::knx::server_event_handler on_event {};
    };

    /// @brief Accepts KNXnet/IP TCP connections and serves each on its own task.
    class tcp_server final
    {
    public:
        /// @brief Creates the loop; nothing is bound until @ref listen or @ref serve.
        /// @param exec The executor the listening socket and every connection are registered with.
        /// @param server The server whose channels the connections open.
        /// @param config Where to listen, and the connection limit.
        tcp_server(executor& exec, kmx::aio::knx::generic_server& server, tcp_server_config config = {}) noexcept;
        tcp_server(const tcp_server&) = delete;
        tcp_server& operator=(const tcp_server&) = delete;
        /// @brief Stops the loop and closes the listening socket.
        ~tcp_server() noexcept;

        /// @brief Binds and listens, unless that was done already.
        /// @return Nothing, or why the socket could not be bound, listened on or registered.
        [[nodiscard]] expected_void_t listen() noexcept;

        /// @brief Returns the port listened on; zero before @ref listen has succeeded.
        [[nodiscard]] port_t port() const noexcept { return port_; }

        /// @brief Returns how many connections are being served.
        [[nodiscard]] std::size_t connections() const noexcept { return connections_.load(std::memory_order_relaxed); }

        /// @brief Accepts connections until @ref stop, listening first if that was not done.
        /// @return A task yielding nothing once stopped, or the error that ended the loop.
        [[nodiscard]] task_returning_expected_void_t serve() noexcept(false);

        /// @brief Stops accepting, and ends the receive every connection waits in, which ends its task.
        void stop() noexcept;

    private:
        /// @brief Accepts one connection, waiting in bounded slices.
        [[nodiscard]] task<file_descriptor::expected_t> accept(sockaddr_storage& peer, ::socklen_t& peer_length) noexcept(false);
        /// @brief Serves one accepted connection to its end.
        [[nodiscard]] task<void> serve_connection(file_descriptor connection, sockaddr_storage peer,
                                                  ::socklen_t peer_length) noexcept(false);

        executor& exec_;
        kmx::aio::knx::generic_server& server_;
        tcp_server_config config_ {};
        file_descriptor listener_ {};
        port_t port_ {};
        std::stop_source stop_source_ {};
        std::atomic_size_t connections_ {};
    };
}
#endif // KMX_AIO_FEATURE_READINESS && KMX_AIO_FEATURE_KNX
