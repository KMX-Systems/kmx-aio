/// @file kmx/aio/test/knx/transport.hpp
/// @brief Shared stand-in KNX transport behaviour for the KNX tests.
/// @details Every KNX test that drives a client or a server needs the same two things of a stand-in
/// transport: each outgoing packet kept so the test can inspect what was put on the wire, and an
/// incoming packet copied into the caller's buffer with a peer address attached. Neither is what any
/// individual test is about, so both live here instead of being written out once per test file.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <cstring>
        #include <vector>

        #include <netinet/in.h>
    #endif

    #include <kmx/aio/knx/transport.hpp>

namespace kmx::aio::test::knx
{
    /// @brief A KNX datagram transport that records what was sent through it.
    /// @note Abstract still: a derived transport supplies @c send and @c receive, calling the helpers
    ///       here for the parts every one of them shares.
    class recording_transport: public aio::knx::datagram_transport
    {
    public:
        /// @brief Returns every packet handed to this transport, in order.
        [[nodiscard]] const std::vector<std::vector<std::uint8_t>>& sent_packets() const noexcept { return sent_packets_; }

        /// @brief Returns the peer each of those packets was addressed to, in the same order.
        [[nodiscard]] const std::vector<sockaddr_storage>& sent_peers() const noexcept { return sent_peers_; }

        /// @brief Forgets the packets recorded so far, so a test can inspect one exchange at a time.
        /// @note The peers are deliberately left alone: a test that clears between exchanges still wants
        ///       to see where every packet went across the whole run.
        void clear_sent_packets() noexcept { sent_packets_.clear(); }

        /// @brief Forgets the peers recorded so far.
        void clear_sent_peers() noexcept { sent_peers_.clear(); }

    protected:
        /// @brief Keeps one outgoing packet and the peer it was addressed to.
        /// @param payload The packet as the caller handed it over.
        /// @param peer The destination, which may be null.
        /// @param peer_length How many octets of @p peer are meaningful.
        void record_send(const cspan_byte_t payload, const sockaddr* const peer, const ::socklen_t peer_length)
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
            sent_packets_.emplace_back(bytes, bytes + payload.size());

            sockaddr_storage destination {};
            if ((peer != nullptr) && (peer_length <= sizeof(destination)))
                std::memcpy(&destination, peer, peer_length);
            sent_peers_.push_back(destination);
        }

        /// @brief Copies one packet into a receive buffer.
        /// @param packet The packet to deliver.
        /// @param buffer The caller's buffer.
        /// @return How many octets were delivered, or that the packet does not fit.
        [[nodiscard]] static expected_size_t deliver(const std::span<const std::uint8_t> packet, const span_byte_t buffer) noexcept
        {
            if (packet.size() > buffer.size())
                return std::unexpected(aio::knx::make_error_code(aio::knx::error::invalid_length));

            std::transform(packet.begin(), packet.end(), buffer.begin(),
                           [](const std::uint8_t value) noexcept { return static_cast<std::byte>(value); });
            return packet.size();
        }

        /// @brief Fills in a loopback peer address of the given family.
        /// @param peer The peer to fill in.
        /// @param ipv6 Whether to write an IPv6 address rather than an IPv4 one.
        /// @param address The IPv4 address in host order; ignored when @p ipv6 is set.
        /// @param port The port, in host order.
        static void fill_peer(aio::knx::transport_peer& peer, const bool ipv6, const std::uint32_t address,
                              const std::uint16_t port) noexcept
        {
            peer = {};
            if (ipv6)
            {
                auto& value = reinterpret_cast<sockaddr_in6&>(peer.address);
                value.sin6_family = AF_INET6;
                value.sin6_addr = in6addr_loopback;
                value.sin6_port = htons(port);
                peer.length = sizeof(sockaddr_in6);
                return;
            }

            auto& value = reinterpret_cast<sockaddr_in&>(peer.address);
            value.sin_family = AF_INET;
            value.sin_addr.s_addr = htonl(address);
            value.sin_port = htons(port);
            peer.length = sizeof(sockaddr_in);
        }

        /// @brief The packets handed to this transport, in order.
        std::vector<std::vector<std::uint8_t>> sent_packets_ {};
        /// @brief The peer each of those packets was addressed to.
        std::vector<sockaddr_storage> sent_peers_ {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
