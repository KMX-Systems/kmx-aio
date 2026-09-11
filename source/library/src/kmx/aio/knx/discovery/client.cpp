/// @file src/kmx/aio/knx/discovery/client.cpp
/// @brief The compiled body of the KNXnet/IP discovery client: search, collect every answer, and describe.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/discovery/client.hpp>
#ifndef PCH
    #include <kmx/aio/knx/error.hpp>

    #include <chrono>
    #include <cstddef>
    #include <cstring>
    #include <arpa/inet.h>
    #include <netinet/in.h>
#endif

namespace kmx::aio::knx::discovery
{
    bool client::address_is_multicast() const noexcept
    {
        if (peer_.ss_family == AF_INET)
        {
            const auto address = ntohl(reinterpret_cast<const sockaddr_in&>(peer_).sin_addr.s_addr);
            return (address >> 28u) == 0xEu; // 224.0.0.0/4
        }

        if (peer_.ss_family == AF_INET6)
            return reinterpret_cast<const sockaddr_in6&>(peer_).sin6_addr.s6_addr[0u] == 0xFFu; // ff00::/8
        return false;
    }

    std::uint32_t client::now_ms() const noexcept
    {
        if (clock_now_ != nullptr)
            return clock_now_();

        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    bool client::peer_matches(const transport_peer& peer) const noexcept
    {
        if ((peer_length_ == 0u) || (peer_length_ > sizeof(sockaddr_storage)) || (peer.length == 0u) ||
            (peer.length > sizeof(sockaddr_storage)))
            return false;

        // A search sent to the discovery multicast group is answered by each server from its own unicast
        // address, so the source of an answer is never the address the request went to. Comparing them
        // rejects every real discovery response, which is what made multicast search unusable while the
        // loopback unicast tests stayed green. Only the family is required to match.
        if (peer_is_multicast_)
            return peer.address.ss_family == peer_.ss_family;

        if (peer.address.ss_family != peer_.ss_family)
            return false;
        if (peer_.ss_family == AF_INET)
        {
            if ((peer_length_ < sizeof(sockaddr_in)) || (peer.length < sizeof(sockaddr_in)))
                return false;
            const auto& expected = reinterpret_cast<const sockaddr_in&>(peer_);
            const auto& actual = reinterpret_cast<const sockaddr_in&>(peer.address);
            return (expected.sin_port == actual.sin_port) && (expected.sin_addr.s_addr == actual.sin_addr.s_addr);
        }

        if (peer_.ss_family == AF_INET6)
        {
            if ((peer_length_ < sizeof(sockaddr_in6)) || (peer.length < sizeof(sockaddr_in6)))
                return false;
            const auto& expected = reinterpret_cast<const sockaddr_in6&>(peer_);
            const auto& actual = reinterpret_cast<const sockaddr_in6&>(peer.address);
            return (expected.sin6_port == actual.sin6_port) && (expected.sin6_scope_id == actual.sin6_scope_id) &&
                   (std::memcmp(&expected.sin6_addr, &actual.sin6_addr, sizeof(expected.sin6_addr)) == 0);
        }

        return false;
    }

    task_returning_expected_void_t client::send_packet(const cspan_uint8_t packet) noexcept(false)
    {
        const auto* bytes = reinterpret_cast<const std::byte*>(packet.data());
        const auto sent =
            co_await transport_.send(cspan_byte_t {bytes, packet.size()}, reinterpret_cast<const sockaddr*>(&peer_), peer_length_);
        if (!sent)
            co_return std::unexpected(sent.error());
        if (*sent != packet.size())
            co_return std::unexpected(make_error_code(error::connection_failed));
        co_return expected_void_t {};
    }

    search_task_t client::search_packet(const cspan_uint8_t packet) noexcept(false)
    {
        if (const auto sent = co_await send_packet(packet); !sent)
            co_return std::unexpected(sent.error());

        transport_peer peer {};
        const auto received = co_await transport_.receive(span_byte_t {reinterpret_cast<std::byte*>(buffer_.data()), buffer_.size()}, peer);
        if (!received)
            co_return std::unexpected(received.error());
        if (!peer_matches(peer))
            co_return std::unexpected(make_error_code(error::connection_failed));
        if ((received.value() == 0u) || (received.value() > buffer_.size()))
            co_return std::unexpected(make_error_code(error::invalid_length));

        const auto decoded = decode_search_response_packet({buffer_.data(), received.value()});
        if (decoded.has_value())
            co_return search_response {decoded.value()};
        const auto decoded_ipv6 = decode_ipv6_search_response_packet({buffer_.data(), received.value()});
        if (!decoded_ipv6.has_value())
            co_return std::unexpected(decoded.error());
        co_return search_response {decoded_ipv6.value()};
    }

    search_task_t client::search(const search_request_frame& request) noexcept(false)
    {
        std::array<std::uint8_t, frame::communication_header_size + search_request_body_size> packet {};
        const auto encoded = encode_search_request_packet(packet, request);
        if (!encoded.has_value())
            co_return std::unexpected(encoded.error());
        co_return co_await search_packet(packet);
    }

    search_task_t client::search(const ipv6_search_request_frame& request) noexcept(false)
    {
        std::array<std::uint8_t, frame::communication_header_size + ipv6_search_request_body_size> packet {};
        const auto encoded = encode_ipv6_search_request_packet(packet, request);
        if (!encoded.has_value())
            co_return std::unexpected(encoded.error());
        co_return co_await search_packet(packet);
    }

    void client::collect_response(const cspan_uint8_t datagram, const transport_peer& peer, std::vector<discovered_server>& found) noexcept
    {
        const auto header = frame::decode_communication_header(datagram);
        // A datagram of exactly the header size decodes a header and has no HPAI behind it, so the
        // structure length read below needs an octet to exist before it is read.
        if (!header.has_value() || (datagram.size() <= frame::communication_header_size))
            return;

        // Dispatched rather than tried in turn. Attempting each decoder until one succeeds re-parses the
        // header once per attempt and, worse, reads a rejection as "not this kind of response" when it may
        // equally mean "this kind, malformed".
        if (header->service_type == search_response_extended_service)
        {
            if (const auto decoded = decode_extended_search_response_packet(datagram); decoded.has_value())
                found.push_back(discovered_server {search_response {decoded.value()}, peer.address, peer.length});
            return;
        }

        if (header->service_type != search_response_service)
            return;

        // IPv4 and IPv6 SEARCH_RESPONSE share one service type and are told apart only by the structure
        // length of the HPAI behind the header, which is what both decoders check first.
        if (datagram[frame::communication_header_size] == static_cast<std::uint8_t>(connection::ipv6_hpai_size))
        {
            if (const auto decoded = decode_ipv6_search_response_packet(datagram); decoded.has_value())
                found.push_back(discovered_server {search_response {decoded.value()}, peer.address, peer.length});
            return;
        }

        if (const auto decoded = decode_search_response_packet(datagram); decoded.has_value())
            found.push_back(discovered_server {search_response {decoded.value()}, peer.address, peer.length});
    }

    search_all_task_t client::search_all_packet(const cspan_uint8_t packet) noexcept(false)
    {
        if (const auto sent = co_await send_packet(packet); !sent)
            co_return std::unexpected(sent.error());

        // Collected until the window closes rather than stopping at the first answer: a search is one
        // request to every server on the subnet, and returning the fastest of them hides the rest.
        std::vector<discovered_server> found;
        // A subnet search answers from a handful of servers; reserving that many means the common search
        // never reallocates while it is collecting.
        found.reserve(16u);
        const auto deadline = now_ms() + config_.search_timeout_ms;
        for (;;)
        {
            transport_peer peer {};
            const auto received = co_await transport_.receive_until(
                span_byte_t {reinterpret_cast<std::byte*>(buffer_.data()), buffer_.size()}, peer, deadline);
            if (!received)
            {
                // The window closing is the normal end of a search, not a failure. Anything else is.
                if (received.error() == make_error_code(error::timeout))
                    co_return found;
                co_return std::unexpected(received.error());
            }

            if (static_cast<std::int32_t>(now_ms() - deadline) >= 0)
                co_return found;
            if ((received.value() == 0u) || (received.value() > buffer_.size()) || !peer_matches(peer))
                continue; // a stray datagram does not end a search that other servers may still answer

            collect_response({buffer_.data(), received.value()}, peer, found);
        }
    }

    search_all_task_t client::search_all(const search_request_frame& request) noexcept(false)
    {
        std::array<std::uint8_t, frame::communication_header_size + search_request_body_size> packet {};
        const auto encoded = encode_search_request_packet(packet, request);
        if (!encoded.has_value())
            co_return std::unexpected(encoded.error());
        co_return co_await search_all_packet(packet);
    }

    search_all_task_t client::search_all(const extended_search_request_frame& request) noexcept(false)
    {
        const auto size = extended_search_request_size(request);
        if (!size.has_value())
            co_return std::unexpected(size.error());

        byte_buffer_t packet(*size, 0u);
        const auto encoded = encode_extended_search_request_packet(packet, request);
        if (!encoded.has_value())
            co_return std::unexpected(encoded.error());
        co_return co_await search_all_packet(packet);
    }

    search_all_task_t client::search_all(const ipv6_search_request_frame& request) noexcept(false)
    {
        std::array<std::uint8_t, frame::communication_header_size + ipv6_search_request_body_size> packet {};
        const auto encoded = encode_ipv6_search_request_packet(packet, request);
        if (!encoded.has_value())
            co_return std::unexpected(encoded.error());
        co_return co_await search_all_packet(packet);
    }

    description_task_t client::describe(const description_request_frame& request) noexcept(false)
    {
        std::array<std::uint8_t, frame::communication_header_size + description_request_body_size> packet {};
        const auto encoded = encode_description_request_packet(packet, request);
        if (!encoded.has_value())
            co_return std::unexpected(encoded.error());
        if (const auto sent = co_await send_packet(packet); !sent)
            co_return std::unexpected(sent.error());

        transport_peer peer {};
        const auto deadline = now_ms() + config_.response_timeout_ms;
        const auto received =
            co_await transport_.receive_until(span_byte_t {reinterpret_cast<std::byte*>(buffer_.data()), buffer_.size()}, peer, deadline);
        if (!received)
            co_return std::unexpected(received.error());
        if (!peer_matches(peer))
            co_return std::unexpected(make_error_code(error::connection_failed));
        if ((received.value() == 0u) || (received.value() > buffer_.size()))
            co_return std::unexpected(make_error_code(error::invalid_length));

        co_return decode_description_response_packet({buffer_.data(), received.value()});
    }
}
