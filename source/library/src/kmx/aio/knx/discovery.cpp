/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/discovery.hpp>

#include <kmx/aio/knx/dib.hpp>

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <optional>

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
        if ((peer_length_ == 0u) || (peer_length_ > sizeof(sockaddr_storage)) ||
            (peer.length == 0u) || (peer.length > sizeof(sockaddr_storage)))
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
        const auto received = co_await transport_.receive(
            span_byte_t {reinterpret_cast<std::byte*>(buffer_.data()), buffer_.size()}, peer);
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

    // One definition of what a well-formed run of description blocks is, in the module that models them.
    // Two copies of this walk would be two chances for a decoder and a validator to disagree about which
    // datagrams are acceptable.
    static bool valid_dibs(const cspan_uint8_t dibs) noexcept
    {
        return dib::valid_blocks(dibs);
    }

    static std::expected<hpai, std::error_code> decode_discovery_hpai(const cspan_uint8_t source) noexcept
    {
        if (!source.empty() && (source[0] == 20u))
            return std::unexpected(make_error_code(error::unsupported_hpai));
        if ((source.size() < connection::hpai_size) || (source[0] != connection::hpai_size))
            return std::unexpected(make_error_code(error::malformed_frame));
        if (source[1] != 0x01u)
            return std::unexpected(make_error_code(error::unsupported_hpai));

        hpai value {};
        value.protocol = source[1];
        value.endpoint.address = { source[2], source[3], source[4], source[5] };
        value.endpoint.port = static_cast<std::uint16_t>((static_cast<std::uint16_t>(source[6]) << 8u) |
                                                          static_cast<std::uint16_t>(source[7]));
        return value;
    }

    static void encode_discovery_hpai(const span_uint8_t dest, const hpai& value) noexcept
    {
        dest[0] = static_cast<std::uint8_t>(connection::hpai_size);
        dest[1] = value.protocol;
        dest[2] = value.endpoint.address[0];
        dest[3] = value.endpoint.address[1];
        dest[4] = value.endpoint.address[2];
        dest[5] = value.endpoint.address[3];
        dest[6] = static_cast<std::uint8_t>((value.endpoint.port >> 8u) & 0xFFu);
        dest[7] = static_cast<std::uint8_t>(value.endpoint.port & 0xFFu);
    }

    static void encode_ipv6_discovery_hpai(const span_uint8_t dest, const ipv6_hpai& value) noexcept
    {
        dest[0] = static_cast<std::uint8_t>(connection::ipv6_hpai_size);
        dest[1] = value.protocol;
        std::copy_n(value.endpoint.address.begin(), value.endpoint.address.size(), dest.begin() + 2u);
        dest[18u] = static_cast<std::uint8_t>(value.endpoint.port >> 8u);
        dest[19u] = static_cast<std::uint8_t>(value.endpoint.port & 0xFFu);
    }

    static std::expected<ipv6_hpai, std::error_code> decode_ipv6_discovery_hpai(const cspan_uint8_t source) noexcept
    {
        if ((source.size() < connection::ipv6_hpai_size) || (source[0] != connection::ipv6_hpai_size))
            return std::unexpected(make_error_code(error::malformed_frame));
        if (source[1] != 0x01u)
            return std::unexpected(make_error_code(error::unsupported_hpai));
        ipv6_hpai value {};
        value.protocol = source[1];
        std::copy_n(source.begin() + 2u, value.endpoint.address.size(), value.endpoint.address.begin());
        value.endpoint.port = static_cast<std::uint16_t>((static_cast<std::uint16_t>(source[18u]) << 8u) | source[19u]);
        return value;
    }

    expected_void_t encode_search_request_packet(const span_uint8_t dest, const search_request_frame& request) noexcept
    {
        const auto total_length = frame::communication_header_size + search_request_body_size;
        if (dest.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));
        if (request.discovery_endpoint.protocol != 0x01u)
            return std::unexpected(make_error_code(error::unsupported_hpai));

        const auto header = frame::encode_communication_header(dest,
                                                               search_request_service,
                                                               static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        encode_discovery_hpai({ dest.data() + frame::communication_header_size, connection::hpai_size },
                              request.discovery_endpoint);
        return {};
    }

    std::expected<search_request_frame, std::error_code> decode_search_request_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->protocol_version != 0x10u) || (header->service_type != search_request_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        if ((header->total_length != packet.size()) || (packet.size() != frame::communication_header_size + search_request_body_size))
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto endpoint = decode_discovery_hpai({ packet.data() + frame::communication_header_size,
                                                      connection::hpai_size });
        if (!endpoint.has_value())
            return std::unexpected(endpoint.error());

        return search_request_frame { endpoint.value() };
    }

    expected_void_t encode_ipv6_search_request_packet(const span_uint8_t dest, const ipv6_search_request_frame& request) noexcept
    {
        constexpr auto total_length = frame::communication_header_size + ipv6_search_request_body_size;
        if (dest.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));
        if (request.discovery_endpoint.protocol != 0x01u)
            return std::unexpected(make_error_code(error::unsupported_hpai));
        const auto header = frame::encode_communication_header(dest, search_request_service,
                                                               static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());
        encode_ipv6_discovery_hpai({dest.data() + frame::communication_header_size, connection::ipv6_hpai_size},
                                   request.discovery_endpoint);
        return {};
    }

    std::expected<ipv6_search_request_frame, std::error_code> decode_ipv6_search_request_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->protocol_version != 0x10u) || (header->service_type != search_request_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        if ((header->total_length != packet.size()) ||
            (packet.size() != frame::communication_header_size + ipv6_search_request_body_size))
            return std::unexpected(make_error_code(error::malformed_frame));
        const auto endpoint = decode_ipv6_discovery_hpai({packet.data() + frame::communication_header_size,
                                                          connection::ipv6_hpai_size});
        if (!endpoint.has_value())
            return std::unexpected(endpoint.error());
        return ipv6_search_request_frame {endpoint.value()};
    }

    expected_void_t encode_search_response_packet(const span_uint8_t dest, const search_response_frame& response) noexcept
    {
        constexpr std::size_t fixed_body_size = connection::hpai_size;
        const auto total_length = frame::communication_header_size + fixed_body_size + response.device_info_blocks.size();
        if ((total_length > frame::max_frame_size) || (dest.size() < total_length))
            return std::unexpected(make_error_code(error::invalid_length));
        if (response.control_endpoint.protocol != 0x01u)
            return std::unexpected(make_error_code(error::unsupported_hpai));
        if (!valid_dibs(response.device_info_blocks))
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto header = frame::encode_communication_header(dest,
                                                               search_response_service,
                                                               static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        encode_discovery_hpai({ dest.data() + frame::communication_header_size, connection::hpai_size },
                              response.control_endpoint);
        std::copy_n(response.device_info_blocks.begin(), response.device_info_blocks.size(),
                    dest.begin() + frame::communication_header_size + connection::hpai_size);
        return {};
    }

    std::expected<search_response_frame, std::error_code> decode_search_response_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->protocol_version != 0x10u) || (header->service_type != search_response_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        if ((header->total_length != packet.size()) ||
            (packet.size() < frame::communication_header_size + connection::hpai_size + 2u))
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto endpoint = decode_discovery_hpai({ packet.data() + frame::communication_header_size,
                                                      connection::hpai_size });
        if (!endpoint.has_value())
            return std::unexpected(endpoint.error());

        const auto dib_offset = frame::communication_header_size + connection::hpai_size;
        const auto dib_size = packet.size() - dib_offset;
        if (!valid_dibs({ packet.data() + dib_offset, dib_size }))
            return std::unexpected(make_error_code(error::malformed_frame));

        search_response_frame response { endpoint.value(), {} };
        response.device_info_blocks.assign(packet.begin() + dib_offset, packet.end());
        return response;
    }

    expected_void_t encode_ipv6_search_response_packet(const span_uint8_t dest, const ipv6_search_response_frame& response) noexcept
    {
        const auto total_length = frame::communication_header_size + connection::ipv6_hpai_size + response.device_info_blocks.size();
        if ((total_length > frame::max_frame_size) || (dest.size() < total_length))
            return std::unexpected(make_error_code(error::invalid_length));
        if (response.control_endpoint.protocol != 0x01u)
            return std::unexpected(make_error_code(error::unsupported_hpai));
        if (!valid_dibs(response.device_info_blocks))
            return std::unexpected(make_error_code(error::malformed_frame));
        const auto header = frame::encode_communication_header(dest, search_response_service,
                                                               static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());
        encode_ipv6_discovery_hpai({dest.data() + frame::communication_header_size, connection::ipv6_hpai_size},
                                   response.control_endpoint);
        std::copy_n(response.device_info_blocks.begin(), response.device_info_blocks.size(),
                    dest.begin() + frame::communication_header_size + connection::ipv6_hpai_size);
        return {};
    }

    std::expected<ipv6_search_response_frame, std::error_code> decode_ipv6_search_response_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->protocol_version != 0x10u) || (header->service_type != search_response_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        const auto dib_offset = frame::communication_header_size + connection::ipv6_hpai_size;
        if ((header->total_length != packet.size()) || (packet.size() < dib_offset + 2u))
            return std::unexpected(make_error_code(error::malformed_frame));
        const auto endpoint = decode_ipv6_discovery_hpai({packet.data() + frame::communication_header_size,
                                                          connection::ipv6_hpai_size});
        if (!endpoint.has_value())
            return std::unexpected(endpoint.error());
        const auto dib_size = packet.size() - dib_offset;
        if (!valid_dibs({packet.data() + dib_offset, dib_size}))
            return std::unexpected(make_error_code(error::malformed_frame));
        ipv6_search_response_frame response {endpoint.value(), {}};
        response.device_info_blocks.assign(packet.begin() + dib_offset, packet.end());
        return response;
    }

    expected_void_t encode_description_request_packet(const span_uint8_t dest, const description_request_frame& request) noexcept
    {
        constexpr auto total_length = frame::communication_header_size + description_request_body_size;
        if (dest.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));
        if (request.control_endpoint.protocol != 0x01u)
            return std::unexpected(make_error_code(error::unsupported_hpai));

        const auto header =
            frame::encode_communication_header(dest, description_request_service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        encode_discovery_hpai({dest.data() + frame::communication_header_size, connection::hpai_size}, request.control_endpoint);
        return {};
    }

    std::expected<description_request_frame, std::error_code> decode_description_request_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->protocol_version != 0x10u) || (header->service_type != description_request_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        constexpr auto total_length = frame::communication_header_size + description_request_body_size;
        if ((header->total_length != packet.size()) || (packet.size() != total_length))
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto endpoint = decode_discovery_hpai({packet.data() + frame::communication_header_size, connection::hpai_size});
        if (!endpoint.has_value())
            return std::unexpected(endpoint.error());

        return description_request_frame {endpoint.value()};
    }

    expected_void_t encode_description_response_packet(const span_uint8_t dest, const description_response_frame& response) noexcept
    {
        const auto total_length = frame::communication_header_size + response.device_info_blocks.size();
        if ((total_length > frame::max_frame_size) || (dest.size() < total_length))
            return std::unexpected(make_error_code(error::invalid_length));
        if (!valid_dibs(response.device_info_blocks))
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto header = frame::encode_communication_header(dest,
                                                               description_response_service,
                                                               static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());
        std::copy_n(response.device_info_blocks.begin(), response.device_info_blocks.size(),
                    dest.begin() + frame::communication_header_size);
        return {};
    }

    std::expected<description_response_frame, std::error_code> decode_description_response_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->protocol_version != 0x10u) || (header->service_type != description_response_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        if ((header->total_length != packet.size()) || (packet.size() < frame::communication_header_size + 2u))
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto dib_size = packet.size() - frame::communication_header_size;
        const auto dib_offset = frame::communication_header_size;
        if (!valid_dibs({ packet.data() + dib_offset, dib_size }))
            return std::unexpected(make_error_code(error::malformed_frame));

        description_response_frame response {};
        response.device_info_blocks.assign(packet.begin() + dib_offset, packet.end());
        return response;
    }

    void client::collect_response(const cspan_uint8_t datagram, const transport_peer& peer,
                                  std::vector<discovered_server>& found) noexcept
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
        const auto received = co_await transport_.receive_until(
            span_byte_t {reinterpret_cast<std::byte*>(buffer_.data()), buffer_.size()}, peer, deadline);
        if (!received)
            co_return std::unexpected(received.error());
        if (!peer_matches(peer))
            co_return std::unexpected(make_error_code(error::connection_failed));
        if ((received.value() == 0u) || (received.value() > buffer_.size()))
            co_return std::unexpected(make_error_code(error::invalid_length));

        co_return decode_description_response_packet({buffer_.data(), received.value()});
    }

    /// @brief Returns the fixed data size a search request parameter type requires, if it has one.
    /// @param type The parameter type.
    /// @return The required data size, or nothing when the type's data is variable.
    [[nodiscard]] static constexpr std::optional<std::size_t> search_parameter_data_size(const search_parameter_type type) noexcept
    {
        switch (type)
        {
            case search_parameter_type::programming_mode:
                return std::size_t {0u};
            case search_parameter_type::mac_address:
                return std::size_t {6u};
            case search_parameter_type::service:
                return std::size_t {2u};
            case search_parameter_type::request_dibs:
                return std::nullopt; // a list of DIB type codes, padded to an even length
        }
        return std::nullopt;
    }

    [[nodiscard]] static constexpr bool known_search_parameter(const std::uint8_t type) noexcept
    {
        switch (static_cast<search_parameter_type>(type))
        {
            case search_parameter_type::programming_mode:
            case search_parameter_type::mac_address:
            case search_parameter_type::service:
            case search_parameter_type::request_dibs:
                return true;
        }
        return false;
    }

    /// @brief Returns the encoded size of one parameter block, padding the variable-length one to even.
    [[nodiscard]] static std::expected<std::size_t, std::error_code> parameter_block_size(const search_parameter& value) noexcept
    {
        if (!known_search_parameter(static_cast<std::uint8_t>(value.type)))
            return std::unexpected(make_error_code(error::invalid_configuration));

        const auto required = search_parameter_data_size(value.type);
        if (required.has_value() && (value.data.size() != *required))
            return std::unexpected(make_error_code(error::invalid_configuration));

        auto size = search_parameter_header_size + value.data.size();
        // A parameter block is an even number of octets; the DIB list is the only one that can be odd.
        size += (size % 2u);
        if (size > 0xFFu)
            return std::unexpected(make_error_code(error::invalid_length));
        return size;
    }

    std::expected<std::size_t, std::error_code> extended_search_request_size(const extended_search_request_frame& request) noexcept
    {
        auto total = frame::communication_header_size + search_request_body_size;
        for (const auto& parameter: request.parameters)
        {
            const auto size = parameter_block_size(parameter);
            if (!size.has_value())
                return std::unexpected(size.error());
            total += *size;
        }
        if (total > frame::max_frame_size)
            return std::unexpected(make_error_code(error::invalid_length));
        return total;
    }

    /// @brief Writes one search parameter block.
    /// @param dest The destination octets.
    /// @param offset Where in @p dest the block starts.
    /// @param parameter The parameter to write.
    /// @return How many octets the block took, or why it could not be written.
    [[nodiscard]] static std::expected<std::size_t, std::error_code> write_search_parameter(
        const span_uint8_t dest, const std::size_t offset, const search_parameter& parameter) noexcept
    {
        // Checked rather than dereferenced blind. It cannot fail here, because the size pass rejected
        // every parameter that would have made it fail - but that is an invariant held in another
        // function, and an encoder is the wrong place to depend on one.
        const auto size = parameter_block_size(parameter);
        if (!size.has_value())
            return std::unexpected(size.error());

        const auto data_offset = offset + search_parameter_header_size;
        dest[offset] = static_cast<std::uint8_t>(*size);
        dest[offset + 1u] = static_cast<std::uint8_t>((parameter.mandatory ? search_parameter_mandatory_mask : 0u) |
                                                      static_cast<std::uint8_t>(parameter.type));
        std::copy_n(parameter.data.begin(), parameter.data.size(), dest.begin() + data_offset);
        // The pad octet, when the data made the block odd, is written as zero.
        std::fill(dest.begin() + data_offset + parameter.data.size(), dest.begin() + offset + *size, std::uint8_t {});
        return size;
    }

    expected_void_t encode_extended_search_request_packet(const span_uint8_t dest,
                                                           const extended_search_request_frame& request) noexcept
    {
        const auto total_length = extended_search_request_size(request);
        if (!total_length.has_value())
            return std::unexpected(total_length.error());
        if (dest.size() < *total_length)
            return std::unexpected(make_error_code(error::invalid_length));
        if (request.discovery_endpoint.protocol != 0x01u)
            return std::unexpected(make_error_code(error::unsupported_hpai));

        const auto header = frame::encode_communication_header(dest, search_request_extended_service,
                                                               static_cast<std::uint16_t>(*total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        encode_discovery_hpai({dest.data() + frame::communication_header_size, connection::hpai_size}, request.discovery_endpoint);

        auto offset = frame::communication_header_size + search_request_body_size;
        for (const auto& parameter: request.parameters)
        {
            const auto written = write_search_parameter(dest, offset, parameter);
            if (!written.has_value())
                return std::unexpected(written.error());
            offset += *written;
        }
        return {};
    }

    /// @brief Checks a datagram is a SEARCH_REQUEST_EXTENDED long enough to hold its fixed part.
    /// @param packet The received datagram, header included.
    /// @return Nothing, or why the datagram is not one this decoder reads.
    [[nodiscard]] static expected_void_t validate_extended_search_header(const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->protocol_version != 0x10u) || (header->service_type != search_request_extended_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        if (header->total_length != packet.size())
            return std::unexpected(make_error_code(error::malformed_frame));
        if (packet.size() < frame::communication_header_size + search_request_body_size)
            return std::unexpected(make_error_code(error::malformed_frame));
        return {};
    }

    /// @brief A decoded parameter, nothing when the block is an unknown optional one to skip.
    using optional_search_parameter_result_t = std::expected<std::optional<search_parameter>, std::error_code>;

    /// @brief Decodes one search parameter block.
    /// @param block The block's octets, its own two-octet prologue included.
    /// @return The parameter, nothing when it is an unknown one the searcher did not insist on, or the
    ///         reason the block cannot be read.
    [[nodiscard]] static optional_search_parameter_result_t decode_search_parameter(const cspan_uint8_t block) noexcept
    {
        const auto type_octet = block[1u];
        const auto type = static_cast<std::uint8_t>(type_octet & search_parameter_type_mask);
        const auto mandatory = (type_octet & search_parameter_mandatory_mask) != 0u;
        if (!known_search_parameter(type))
        {
            // A parameter this build has no name for is skipped, unless the searcher marked it mandatory -
            // in which case answering without honouring it would be answering a different search.
            if (mandatory)
                return std::unexpected(make_error_code(error::unsupported_service));
            return std::optional<search_parameter> {};
        }

        search_parameter parameter {};
        parameter.mandatory = mandatory;
        parameter.type = static_cast<search_parameter_type>(type);

        const auto required = search_parameter_data_size(parameter.type);
        // The fixed-size parameters state their own size twice - in the block length and in the
        // specification - and a block whose length disagrees is not the parameter it claims to be.
        if (required.has_value() && ((block.size() - search_parameter_header_size) != *required))
            return std::unexpected(make_error_code(error::malformed_frame));

        parameter.data.assign(block.begin() + search_parameter_header_size, block.end());
        return std::optional<search_parameter> {std::move(parameter)};
    }

    std::expected<extended_search_request_frame, std::error_code> decode_extended_search_request_packet(
        const cspan_uint8_t packet) noexcept
    {
        if (const auto valid = validate_extended_search_header(packet); !valid.has_value())
            return std::unexpected(valid.error());

        const auto endpoint = decode_discovery_hpai({packet.data() + frame::communication_header_size, connection::hpai_size});
        if (!endpoint.has_value())
            return std::unexpected(endpoint.error());

        extended_search_request_frame decoded {};
        decoded.discovery_endpoint = endpoint.value();

        auto offset = frame::communication_header_size + search_request_body_size;
        while (offset < packet.size())
        {
            const std::size_t size = packet[offset];
            // A block that does not advance, or that runs past the datagram, would loop or read out of
            // bounds; both are rejected rather than salvaged.
            if ((size < search_parameter_header_size) || ((offset + size) > packet.size()))
                return std::unexpected(make_error_code(error::malformed_frame));

            auto parameter = decode_search_parameter(packet.subspan(offset, size));
            if (!parameter.has_value())
                return std::unexpected(parameter.error());
            if (parameter->has_value())
                decoded.parameters.push_back(std::move(**parameter));
            offset += size;
        }
        return decoded;
    }

    expected_void_t encode_extended_search_response_packet(const span_uint8_t dest,
                                                            const extended_search_response_frame& response) noexcept
    {
        const auto total_length = frame::communication_header_size + connection::hpai_size + response.device_info_blocks.size();
        if ((total_length > frame::max_frame_size) || (dest.size() < total_length))
            return std::unexpected(make_error_code(error::invalid_length));
        if (response.control_endpoint.protocol != 0x01u)
            return std::unexpected(make_error_code(error::unsupported_hpai));
        if (!valid_dibs(response.device_info_blocks))
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto header = frame::encode_communication_header(dest, search_response_extended_service,
                                                               static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        encode_discovery_hpai({dest.data() + frame::communication_header_size, connection::hpai_size}, response.control_endpoint);
        std::copy_n(response.device_info_blocks.begin(), response.device_info_blocks.size(),
                    dest.begin() + frame::communication_header_size + connection::hpai_size);
        return {};
    }

    std::expected<extended_search_response_frame, std::error_code> decode_extended_search_response_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->protocol_version != 0x10u) || (header->service_type != search_response_extended_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        if (header->total_length != packet.size())
            return std::unexpected(make_error_code(error::malformed_frame));
        if (packet.size() < frame::communication_header_size + connection::hpai_size)
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto endpoint = decode_discovery_hpai({packet.data() + frame::communication_header_size, connection::hpai_size});
        if (!endpoint.has_value())
            return std::unexpected(endpoint.error());

        const auto dib_offset = frame::communication_header_size + connection::hpai_size;
        if (!valid_dibs(packet.subspan(dib_offset)))
            return std::unexpected(make_error_code(error::malformed_frame));

        extended_search_response_frame response {endpoint.value(), {}};
        response.device_info_blocks.assign(packet.begin() + dib_offset, packet.end());
        return response;
    }
}
