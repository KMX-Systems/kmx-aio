/// @file aio/knx/gateway.hpp
/// @brief Composition wrapper for a KNX tunnelling server and routing client.
/// @details
/// A KNXnet/IP gateway is what sits between IP clients and the bus: it accepts tunnelling connections on
/// one side and speaks routing multicast on the other. Both halves already exist as
/// @ref kmx::aio::knx::generic_server and @ref kmx::aio::knx::routing::client, and neither needs to know
/// about the other, so this is a facade rather than a new protocol layer - it owns the two objects, drives
/// their lifetimes together, and hands each one out for the traffic only it can carry.
///
/// Forwarding between the two halves is deliberately left to the application. What a gateway should filter
/// and what it should relay is an installation's policy, not a protocol rule, and a wrapper that guessed
/// would have to be worked around rather than used.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <expected>
        #include <system_error>
    #endif

    #include <kmx/aio/task.hpp>
    #include <kmx/aio/knx/routing.hpp>
    #include <kmx/aio/knx/server.hpp>

namespace kmx::aio::knx
{
    /// @brief A KNXnet/IP tunnelling server and a routing client driven as one object.
    class gateway final
    {
    public:
        /// @brief Creates a gateway over one transport.
        /// @param transport The executor-bound UDP transport both halves drive.
        /// @param server The tunnelling server's configuration.
        /// @param routing The multicast group the routing half joins.
        /// @note Both halves share the transport, so the endpoint has to be one that can carry unicast
        ///       tunnelling traffic and the multicast group at once.
        gateway(datagram_transport& transport,
                server_config server = {},
                routing::multicast_configuration routing = {}) noexcept:
            server_(transport, server), router_(transport, routing) {}

        /// @brief Brings both halves up: resets the server's channels, then joins the multicast group.
        /// @return Nothing, or the first error either half reported.
        [[nodiscard]] expected_void_t start() noexcept;

        /// @brief Takes both halves down: shuts the server's channels, then leaves the multicast group.
        /// @return Nothing, or the first error either half reported.
        /// @note Both halves are always stopped, even when the first one fails, so a failed shutdown does
        ///       not leave the multicast membership behind.
        [[nodiscard]] expected_void_t stop() noexcept;

        /// @brief Takes both halves down.
        /// @return Nothing, or the first error either half reported.
        /// @note Spelled to match the other server-shaped types in this library; it is @ref stop.
        [[nodiscard]] expected_void_t shutdown() noexcept { return stop(); }

        /// @brief Runs the tunnelling half until one event is produced.
        /// @return A task yielding the event, or the error that stopped the exchange.
        [[nodiscard]] server_event_task_t serve_once() noexcept(false)
        {
            co_return co_await server_.serve_once();
        }

        /// @brief Runs the tunnelling half until it stops.
        /// @return A task yielding nothing, or the error that ended the loop.
        [[nodiscard]] task_returning_expected_void_t serve() noexcept(false)
        {
            co_return co_await server_.serve();
        }

        /// @brief Returns the tunnelling half, for sending onto an established channel.
        [[nodiscard]] generic_server& server() noexcept { return server_; }

        /// @brief Returns the routing half, for sending and receiving multicast indications.
        [[nodiscard]] routing::client& router() noexcept { return router_; }

    private:
        generic_server server_;
        routing::client router_;
    };
}
#endif // KMX_AIO_FEATURE_KNX
