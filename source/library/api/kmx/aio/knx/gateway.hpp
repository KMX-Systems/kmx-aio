/// @file api/kmx/aio/knx/gateway.hpp
/// @brief Composition wrapper for a KNX tunnelling server and routing client.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
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
///
/// Either half may be secure: a server built with @ref kmx::aio::knx::server_config::secure, and a router built with a
/// @ref kmx::aio::knx::routing::secure_configuration. The gateway reports which halves are, so a forwarding policy can
/// refuse to relay what arrived on a secured half onto one that is not. KNX Data Secure APDUs cross either half untouched:
/// the gateway holds no group keys, and neither half opens them.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/knx/generic_server.hpp>
        #include <kmx/aio/knx/routing.hpp>
        #include <kmx/aio/knx/routing/client.hpp>
        #include <kmx/aio/knx/secure/common.hpp>
        #include <kmx/aio/knx/secure/entropy_source.hpp>
        #include <kmx/aio/knx/server.hpp>
        #include <kmx/aio/task.hpp>

        #include <expected>
        #include <system_error>
        #include <utility>
    #endif

namespace kmx::aio::knx
{
    /// @brief What a gateway's routing half joins a KNX IP Secure backbone with, and what both halves' secure state runs on.
    struct secure_gateway_options
    {
        /// @brief The backbone key, latency tolerance and serial number of the routing half.
        routing::secure_configuration routing_settings {};
        /// @brief The clock both halves' secure state runs on; the steady clock when null.
        secure::monotonic_ms_function clock_ms {};
        /// @brief Where both halves draw keys, message tags and delays from; @ref kmx::aio::knx::secure::system_entropy when
        ///        null. Must outlive the gateway.
        secure::entropy_source* entropy {};
    };

    /// @brief A KNXnet/IP tunnelling server and a routing client driven as one object.
    class gateway final
    {
    public:
        /// @brief Creates a gateway over one transport.
        /// @param transport The executor-bound UDP transport both halves drive.
        /// @param server The tunnelling server's configuration.
        /// @param routing The multicast group the routing half joins.
        /// @throws std::bad_alloc when the server's secure sessions cannot be allocated.
        /// @note Both halves share the transport, so the endpoint has to be one that can carry unicast
        ///       tunnelling traffic and the multicast group at once.
        explicit gateway(datagram_transport& transport, server_config server = {},
                         routing::multicast_configuration routing = {}) noexcept(false):
            server_(transport, std::move(server)),
            router_(transport, routing)
        {
        }

        /// @brief Creates a gateway whose routing half is KNX IP Secure.
        /// @param transport The executor-bound UDP transport both halves drive.
        /// @param server The tunnelling server's configuration; set @ref server_config::secure for secure tunnelling, which
        ///        is served over TCP by @ref generic_server::serve_connection.
        /// @param routing The multicast group the routing half joins.
        /// @param options The backbone key, latency tolerance and serial number of the routing half, and the clock and
        ///        entropy both halves' secure state runs on.
        /// @throws std::bad_alloc when either half's secure state cannot be allocated.
        gateway(datagram_transport& transport, server_config server, routing::multicast_configuration routing,
                secure_gateway_options options) noexcept(false):
            server_(transport, std::move(server), server_options {.secure_clock_ms = options.clock_ms, .entropy = options.entropy}),
            router_(transport, routing,
                    routing::secure_options {
                        .settings = std::move(options.routing_settings), .clock_ms = options.clock_ms, .entropy = options.entropy})
        {
        }

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
        [[nodiscard]] server_event_task_t serve_once() noexcept(false) { co_return co_await server_.serve_once(); }

        /// @brief Runs the tunnelling half until it stops.
        /// @return A task yielding nothing, or the error that ended the loop.
        [[nodiscard]] task_returning_expected_void_t serve() noexcept(false) { co_return co_await server_.serve(); }

        /// @brief Returns the tunnelling half, for sending onto an established channel.
        [[nodiscard]] generic_server& server() noexcept { return server_; }

        /// @brief Returns the routing half, for sending and receiving multicast indications.
        [[nodiscard]] routing::client& router() noexcept { return router_; }

        /// @brief Indicates whether the tunnelling half is KNX IP Secure.
        [[nodiscard]] bool server_secured() const noexcept { return server_.secured(); }

        /// @brief Indicates whether the routing half is KNX IP Secure.
        [[nodiscard]] bool router_secured() const noexcept { return router_.secured(); }

    private:
        generic_server server_;
        routing::client router_;
    };
}
#endif // KMX_AIO_FEATURE_KNX
