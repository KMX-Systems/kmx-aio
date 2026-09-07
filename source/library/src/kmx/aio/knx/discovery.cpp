/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/discovery.hpp>

#include <cstring>
#include <netinet/in.h>

namespace kmx::aio::knx::discovery
{
    bool client::peer_matches(const transport_peer& peer) const noexcept
    {
        if ((peer_length_ == 0u) || (peer_length_ > sizeof(sockaddr_storage)) ||
            (peer.length == 0u) || (peer.length > sizeof(sockaddr_storage)) ||
            (peer.address.ss_family != peer_.ss_family))
            return false;
        if (peer_.ss_family == AF_INET)
        {
            if ((peer_length_ < sizeof(sockaddr_in)) || (peer.length < sizeof(sockaddr_in)))
                return false;
            const auto& expected = reinterpret_cast<const sockaddr_in&>(peer_);
            const auto& actual = reinterpret_cast<const sockaddr_in&>(peer.address);
            return expected.sin_port == actual.sin_port && expected.sin_addr.s_addr == actual.sin_addr.s_addr;
        }
        if (peer_.ss_family == AF_INET6)
        {
            if ((peer_length_ < sizeof(sockaddr_in6)) || (peer.length < sizeof(sockaddr_in6)))
                return false;
            const auto& expected = reinterpret_cast<const sockaddr_in6&>(peer_);
            const auto& actual = reinterpret_cast<const sockaddr_in6&>(peer.address);
            return expected.sin6_port == actual.sin6_port && expected.sin6_scope_id == actual.sin6_scope_id &&
                   std::memcmp(&expected.sin6_addr, &actual.sin6_addr, sizeof(expected.sin6_addr)) == 0;
        }
        return false;
    }

    task<std::expected<search_response, std::error_code>> client::search_packet(
        const cspan_uint8_t packet) noexcept(false)
    {
        const auto* bytes = reinterpret_cast<const std::byte*>(packet.data());
        const auto sent = co_await transport_.send(
            cspan_byte_t {bytes, packet.size()}, reinterpret_cast<const sockaddr*>(&peer_), peer_length_);
        if (!sent)
            co_return std::unexpected(sent.error());
        if (*sent != packet.size())
            co_return std::unexpected(make_error_code(error::connection_failed));

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

    task<std::expected<search_response, std::error_code>> client::search(
        const search_request_frame& request) noexcept(false)
    {
        std::array<std::uint8_t, frame::communication_header_size + search_request_body_size> packet {};
        const auto encoded = encode_search_request_packet(packet, request);
        if (!encoded.has_value())
            co_return std::unexpected(encoded.error());
        co_return co_await search_packet(packet);
    }

    task<std::expected<search_response, std::error_code>> client::search(
        const ipv6_search_request_frame& request) noexcept(false)
    {
        std::array<std::uint8_t, frame::communication_header_size + ipv6_search_request_body_size> packet {};
        const auto encoded = encode_ipv6_search_request_packet(packet, request);
        if (!encoded.has_value())
            co_return std::unexpected(encoded.error());
        co_return co_await search_packet(packet);
    }

    namespace
    {
        bool valid_dibs(const cspan_uint8_t dibs) noexcept
        {
            std::size_t offset = 0u;
            while (offset < dibs.size())
            {
                const auto remaining = dibs.size() - offset;
                const auto length = static_cast<std::size_t>(dibs[offset]);
                if ((length < 2u) || (length > remaining))
                    return false;
                offset += length;
            }
            return offset == dibs.size();
        }

        std::expected<hpai, std::error_code> decode_discovery_hpai(const cspan_uint8_t source) noexcept
        {
            if (!source.empty() && source[0] == 20u)
                return std::unexpected(make_error_code(error::unsupported_hpai));
            if (source.size() < connection::hpai_size || source[0] != connection::hpai_size)
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

        void encode_discovery_hpai(const span_uint8_t dest, const hpai& value) noexcept
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

        void encode_ipv6_discovery_hpai(const span_uint8_t dest, const ipv6_hpai& value) noexcept
        {
            dest[0] = static_cast<std::uint8_t>(connection::ipv6_hpai_size);
            dest[1] = value.protocol;
            for (std::size_t i = 0u; i < value.endpoint.address.size(); ++i)
                dest[2u + i] = value.endpoint.address[i];
            dest[18u] = static_cast<std::uint8_t>(value.endpoint.port >> 8u);
            dest[19u] = static_cast<std::uint8_t>(value.endpoint.port & 0xFFu);
        }

        std::expected<ipv6_hpai, std::error_code> decode_ipv6_discovery_hpai(const cspan_uint8_t source) noexcept
        {
            if ((source.size() < connection::ipv6_hpai_size) || (source[0] != connection::ipv6_hpai_size))
                return std::unexpected(make_error_code(error::malformed_frame));
            if (source[1] != 0x01u)
                return std::unexpected(make_error_code(error::unsupported_hpai));
            ipv6_hpai value {};
            value.protocol = source[1];
            for (std::size_t i = 0u; i < value.endpoint.address.size(); ++i)
                value.endpoint.address[i] = source[2u + i];
            value.endpoint.port = static_cast<std::uint16_t>((static_cast<std::uint16_t>(source[18u]) << 8u) | source[19u]);
            return value;
        }
    }

    std::expected<void, std::error_code> encode_search_request_packet(
        const span_uint8_t dest, const search_request_frame& request) noexcept
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
        if (header->protocol_version != 0x10u || header->service_type != search_request_service)
            return std::unexpected(make_error_code(error::unsupported_service));
        if ((header->total_length != packet.size()) || (packet.size() != frame::communication_header_size + search_request_body_size))
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto endpoint = decode_discovery_hpai({ packet.data() + frame::communication_header_size,
                                                      connection::hpai_size });
        if (!endpoint.has_value())
            return std::unexpected(endpoint.error());

        return search_request_frame { endpoint.value() };
    }

    std::expected<void, std::error_code> encode_ipv6_search_request_packet(
        const span_uint8_t dest, const ipv6_search_request_frame& request) noexcept
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

    std::expected<void, std::error_code> encode_search_response_packet(
        const span_uint8_t dest, const search_response_frame& response) noexcept
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
        for (std::size_t i = 0u; i < response.device_info_blocks.size(); ++i)
            dest[frame::communication_header_size + connection::hpai_size + i] = response.device_info_blocks[i];
        return {};
    }

    std::expected<search_response_frame, std::error_code> decode_search_response_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if (header->protocol_version != 0x10u || header->service_type != search_response_service)
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

    std::expected<void, std::error_code> encode_ipv6_search_response_packet(
        const span_uint8_t dest, const ipv6_search_response_frame& response) noexcept
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
        for (std::size_t i = 0u; i < response.device_info_blocks.size(); ++i)
            dest[frame::communication_header_size + connection::ipv6_hpai_size + i] = response.device_info_blocks[i];
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

    std::expected<void, std::error_code> encode_description_request_packet(const span_uint8_t dest) noexcept
    {
        if (dest.size() < frame::communication_header_size)
            return std::unexpected(make_error_code(error::invalid_length));

        return frame::encode_communication_header(dest,
                                                  description_request_service,
                                                  static_cast<std::uint16_t>(frame::communication_header_size));
    }

    std::expected<description_request_frame, std::error_code> decode_description_request_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if ((header->protocol_version != 0x10u) || (header->service_type != description_request_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        if (header->total_length != packet.size() || packet.size() != frame::communication_header_size)
            return std::unexpected(make_error_code(error::malformed_frame));

        return description_request_frame {};
    }

    std::expected<void, std::error_code> encode_description_response_packet(
        const span_uint8_t dest, const description_response_frame& response) noexcept
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
        for (std::size_t i = 0u; i < response.device_info_blocks.size(); ++i)
            dest[frame::communication_header_size + i] = response.device_info_blocks[i];
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
}
