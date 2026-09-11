/// @file api/kmx/aio/readiness/knx/udp_transport.hpp
/// @brief Readiness UDP adapter for the executor-neutral KNX transport contract.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// Binds @ref kmx::aio::knx::datagram_transport to a readiness UDP endpoint. The whole KNX stack is
/// written against that contract, so this adapter and its completion twin are the only KNX code either
/// I/O model needs: the protocol itself is compiled once and works under both.
///
/// Nothing about the protocol lives here. The adapter forwards sends and receives to the endpoint, adds
/// the deadline handling the endpoint does not do itself, and translates multicast membership into the
/// socket options that express it.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_READINESS) && defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/knx/datagram_transport.hpp>
        #include <kmx/aio/knx/error.hpp>
        #include <kmx/aio/knx/routing.hpp>
        #include <kmx/aio/knx/transport.hpp>
        #include <kmx/aio/readiness/udp/endpoint.hpp>

        #include <cerrno>
        #include <cstring>
        #include <netinet/in.h>
    #endif

namespace kmx::aio::readiness::knx
{
    /// @brief A readiness UDP endpoint presented as a KNX datagram transport.
    /// @note Holds the endpoint by reference and does not own it; the endpoint must outlive the transport.
    class udp_transport final: public kmx::aio::knx::datagram_transport
    {
    public:
        /// @brief Wraps one endpoint.
        /// @param endpoint The readiness UDP endpoint to drive; must outlive this transport.
        explicit udp_transport(udp::endpoint& endpoint) noexcept: endpoint_(endpoint) {}

        /// @brief Sends one datagram to a peer.
        /// @param payload The octets to send.
        /// @param peer The destination address; only @p peer_length octets are read.
        /// @param peer_length The number of octets @p peer provides.
        /// @return A task yielding the number of octets sent, or the error that stopped the send.
        [[nodiscard]] task_returning_expected_size_t send(const cspan_byte_t payload, const sockaddr* peer,
                                                          const ::socklen_t peer_length) noexcept(false) override
        {
            co_return co_await endpoint_.send(payload, peer, peer_length);
        }

        /// @brief Waits for one datagram, with no deadline.
        /// @param buffer The storage to receive into.
        /// @param peer Filled in with the sender's address and its length.
        /// @return A task yielding the number of octets received, or the error that stopped the receive.
        [[nodiscard]] task_returning_expected_size_t receive(const span_byte_t buffer,
                                                             kmx::aio::knx::transport_peer& peer) noexcept(false) override
        {
            co_return co_await endpoint_.recv(buffer, peer.address, peer.length);
        }

        /// @brief Waits for one datagram, giving up at a deadline.
        /// @param buffer The storage to receive into.
        /// @param peer Filled in with the sender's address and its length.
        /// @param deadline_ms When to give up, as a monotonic millisecond stamp.
        /// @return A task yielding the number of octets received, or the error that stopped the receive.
        /// @note Overridden rather than inherited: the base's default ignores the deadline, and every
        ///       retry the KNX session layer drives depends on this returning.
        [[nodiscard]] task_returning_expected_size_t receive_until(const span_byte_t buffer, kmx::aio::knx::transport_peer& peer,
                                                                   const std::uint32_t deadline_ms) noexcept(false) override;

        /// @brief Joins the routing multicast group on the configured interface.
        /// @param configuration The group, port and interface to join on.
        /// @return Nothing, or the reason the group could not be joined.
        [[nodiscard]] expected_void_t join_multicast_group(
            const kmx::aio::knx::multicast_group_configuration& configuration) noexcept override;

        /// @brief Leaves the routing multicast group.
        /// @param configuration The group the transport previously joined.
        /// @return Nothing, or the reason the group could not be left.
        [[nodiscard]] expected_void_t leave_multicast_group(
            const kmx::aio::knx::multicast_group_configuration& configuration) noexcept override;

    private:
        /// @brief Converts a KNX group address to the `in_addr` the socket option takes.
        [[nodiscard]] static in_addr make_multicast_address(const ipv4::storage_t& group) noexcept;

        udp::endpoint& endpoint_;
    };
}
#endif // KMX_AIO_FEATURE_READINESS && KMX_AIO_FEATURE_KNX
