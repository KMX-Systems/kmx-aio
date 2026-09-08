/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/connection.hpp>

namespace kmx::aio::knx::connection
{
    namespace internal
    {
        constexpr std::uint8_t udp_protocol = 0x01u;
        constexpr std::uint8_t tunnelling_type = tunnel_connection_type;
        constexpr std::uint8_t link_layer = tunnel_link_layer;

        bool valid_status(const std::uint8_t value) noexcept
        {
            switch (static_cast<connect_status>(value))
            {
                case connect_status::no_error:
                case connect_status::host_protocol_type:
                case connect_status::version_not_supported:
                case connect_status::sequence_number:
                case connect_status::connection_type:
                case connect_status::connection_option:
                case connect_status::no_more_connections:
                    return true;
            }
            return false;
        }

        void encode_hpai(const span_uint8_t dest, const hpai& value) noexcept
        {
            dest[0] = static_cast<std::uint8_t>(hpai_size);
            dest[1] = value.protocol;
            dest[2] = value.endpoint.address[0];
            dest[3] = value.endpoint.address[1];
            dest[4] = value.endpoint.address[2];
            dest[5] = value.endpoint.address[3];
            dest[6] = static_cast<std::uint8_t>((value.endpoint.port >> 8u) & 0xFFu);
            dest[7] = static_cast<std::uint8_t>(value.endpoint.port & 0xFFu);
        }

        void encode_ipv6_hpai(const span_uint8_t dest, const ipv6_hpai& value) noexcept
        {
            dest[0] = static_cast<std::uint8_t>(ipv6_hpai_size);
            dest[1] = value.protocol;
            for (std::size_t i = 0u; i < value.endpoint.address.size(); ++i)
                dest[2u + i] = value.endpoint.address[i];
            dest[18u] = static_cast<std::uint8_t>(value.endpoint.port >> 8u);
            dest[19u] = static_cast<std::uint8_t>(value.endpoint.port & 0xFFu);
        }

        std::expected<ipv6_hpai, std::error_code> decode_ipv6_hpai(const cspan_uint8_t source) noexcept
        {
            if ((source.size() < ipv6_hpai_size) || (source[0] != ipv6_hpai_size))
                return std::unexpected(make_error_code(error::malformed_frame));
            if (source[1] != udp_protocol)
                return std::unexpected(make_error_code(error::unsupported_hpai));

            ipv6_hpai value {};
            value.protocol = source[1];
            for (std::size_t i = 0u; i < value.endpoint.address.size(); ++i)
                value.endpoint.address[i] = source[2u + i];
            value.endpoint.port = static_cast<std::uint16_t>((static_cast<std::uint16_t>(source[18u]) << 8u) | source[19u]);
            return value;
        }

        std::expected<hpai, std::error_code> decode_hpai(const cspan_uint8_t source) noexcept
        {
            if (!source.empty() && (source[0] == 20u))
                return std::unexpected(make_error_code(error::unsupported_hpai));
            if ((source.size() < hpai_size) || (source[0] != hpai_size))
                return std::unexpected(make_error_code(error::malformed_frame));
            if (source[1] != udp_protocol)
                return std::unexpected(make_error_code(error::unsupported_hpai));

            hpai value {};
            value.protocol = source[1];
            value.endpoint.address = { source[2], source[3], source[4], source[5] };
            value.endpoint.port = static_cast<std::uint16_t>((static_cast<std::uint16_t>(source[6]) << 8u) |
                                                              static_cast<std::uint16_t>(source[7]));
            return value;
        }

        bool valid_header(const communication_header& header, const std::size_t size, const std::uint16_t service) noexcept
        {
            return (header.protocol_version == 0x10u) && (header.service_type == service) &&
                   (header.total_length == size);
        }

        expected_void_t encode_control_packet(const span_uint8_t dest,
                                                                   const std::uint16_t service,
                                                                   const std::uint8_t channel_id,
                                                                   const std::uint8_t status) noexcept
        {
            constexpr std::size_t total_length = frame::communication_header_size + 2u;
            if (dest.size() < total_length)
                return std::unexpected(make_error_code(error::invalid_length));

            const auto header = frame::encode_communication_header(dest, service,
                                                                   static_cast<std::uint16_t>(total_length));
            if (!header.has_value())
                return std::unexpected(header.error());

            dest[6u] = channel_id;
            dest[7u] = status;
            return {};
        }

        /// @brief The channel id and status a control packet carries.
        using control_fields_t = std::array<std::uint8_t, 2u>;
        /// @brief The fields of a control packet, or why they could not be read.
        using control_fields_result_t = std::expected<control_fields_t, std::error_code>;

        control_fields_result_t decode_control_packet(const cspan_uint8_t packet, const std::uint16_t service) noexcept
        {
            const auto header = frame::decode_communication_header(packet);
            if (!header.has_value())
                return std::unexpected(header.error());
            if (!valid_header(header.value(), packet.size(), service))
                return std::unexpected(make_error_code(error::unsupported_service));
            if (packet.size() != frame::communication_header_size + 2u)
                return std::unexpected(make_error_code(error::malformed_frame));

            return control_fields_t {packet[6u], packet[7u]};
        }
    } // namespace internal

    expected_void_t encode_connect_request_packet(const span_uint8_t dest, const connect_request_frame& request) noexcept
    {
        const auto total_length = frame::communication_header_size + connect_request_body_size;
        if (dest.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));
        if ((request.control_endpoint.protocol != internal::udp_protocol) ||
            (request.data_endpoint.protocol != internal::udp_protocol))
            return std::unexpected(make_error_code(error::unsupported_hpai));

        const auto header = frame::encode_communication_header(dest, connect_request_service,
                                                               static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        internal::encode_hpai({ dest.data() + 6u, hpai_size }, request.control_endpoint);
        internal::encode_hpai({ dest.data() + 14u, hpai_size }, request.data_endpoint);
        dest[22u] = connection_information_size;
        dest[23u] = internal::tunnelling_type;
        dest[24u] = internal::link_layer;
        dest[25u] = 0x00u;
        return {};
    }

    std::expected<connect_request_frame, std::error_code> decode_connect_request_packet(const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if (!internal::valid_header(header.value(), packet.size(), connect_request_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        if (packet.size() != frame::communication_header_size + connect_request_body_size)
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto control = internal::decode_hpai({ packet.data() + 6u, hpai_size });
        if (!control.has_value())
            return std::unexpected(control.error());
        const auto data = internal::decode_hpai({packet.data() + 14u, hpai_size});
        if (!data.has_value())
            return std::unexpected(data.error());
        if ((packet[22u] != connection_information_size) || (packet[23u] != internal::tunnelling_type) ||
            (packet[24u] != internal::link_layer) || (packet[25u] != 0x00u))
            return std::unexpected(make_error_code(error::unsupported_connection_type));

        return connect_request_frame { control.value(), data.value() };
    }

    expected_void_t encode_ipv6_connect_request_packet(const span_uint8_t dest, const ipv6_connect_request_frame& request) noexcept
    {
        constexpr auto total_length = frame::communication_header_size + ipv6_connect_request_body_size;
        if (dest.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));
        if ((request.control_endpoint.protocol != internal::udp_protocol) || (request.data_endpoint.protocol != internal::udp_protocol))
            return std::unexpected(make_error_code(error::unsupported_hpai));

        const auto header = frame::encode_communication_header(dest, connect_request_service,
                                                               static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());
        internal::encode_ipv6_hpai({dest.data() + 6u, ipv6_hpai_size}, request.control_endpoint);
        internal::encode_ipv6_hpai({dest.data() + 26u, ipv6_hpai_size}, request.data_endpoint);
        dest[46u] = connection_information_size;
        dest[47u] = internal::tunnelling_type;
        dest[48u] = internal::link_layer;
        dest[49u] = 0u;
        return {};
    }

    std::expected<ipv6_connect_request_frame, std::error_code> decode_ipv6_connect_request_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if (!internal::valid_header(header.value(), packet.size(), connect_request_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        if (packet.size() != frame::communication_header_size + ipv6_connect_request_body_size)
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto control = internal::decode_ipv6_hpai({packet.data() + 6u, ipv6_hpai_size});
        if (!control.has_value())
            return std::unexpected(control.error());
        const auto data = internal::decode_ipv6_hpai({packet.data() + 26u, ipv6_hpai_size});
        if (!data.has_value())
            return std::unexpected(data.error());
        if ((packet[46u] != connection_information_size) || (packet[47u] != internal::tunnelling_type) ||
            (packet[48u] != internal::link_layer) || (packet[49u] != 0u))
            return std::unexpected(make_error_code(error::unsupported_connection_type));
        return ipv6_connect_request_frame {control.value(), data.value()};
    }

    expected_void_t encode_connect_response_packet(const span_uint8_t dest, const connect_response_frame& response) noexcept
    {
        const auto total_length = frame::communication_header_size + connect_response_body_size;
        if (dest.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));
        if (response.data_endpoint.protocol != internal::udp_protocol)
            return std::unexpected(make_error_code(error::unsupported_hpai));
        if (!internal::valid_status(static_cast<std::uint8_t>(response.status)))
            return std::unexpected(make_error_code(error::invalid_configuration));

        const auto header = frame::encode_communication_header(dest, connect_response_service,
                                                               static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        dest[6u] = response.channel_id;
        dest[7u] = static_cast<std::uint8_t>(response.status);
        internal::encode_hpai({ dest.data() + 8u, hpai_size }, response.data_endpoint);
        dest[16u] = connection_information_size;
        dest[17u] = internal::tunnelling_type;
        dest[18u] = static_cast<std::uint8_t>(response.assigned_address.value() >> 8u);
        dest[19u] = static_cast<std::uint8_t>(response.assigned_address.value() & 0xFFu);
        return {};
    }

    std::expected<connect_response_frame, std::error_code> decode_connect_response_packet(const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if (!internal::valid_header(header.value(), packet.size(), connect_response_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        if (packet.size() != frame::communication_header_size + connect_response_body_size)
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto data = internal::decode_hpai({ packet.data() + 8u, hpai_size });
        if (!data.has_value())
            return std::unexpected(data.error());
        if ((packet[16u] != connection_information_size) || (packet[17u] != internal::tunnelling_type))
            return std::unexpected(make_error_code(error::unsupported_connection_type));
        if (!internal::valid_status(packet[7u]))
            return std::unexpected(make_error_code(error::malformed_frame));

        // The last two CRD octets are the individual address the interface assigned to this tunnel. They
        // are data, not a constant: rejecting anything but one fixed value here would refuse every real
        // interface, since each one hands out an address from its own line.
        const individual_address assigned {static_cast<std::uint16_t>((static_cast<std::uint16_t>(packet[18u]) << 8u) | packet[19u])};
        return connect_response_frame {packet[6u], static_cast<connect_status>(packet[7u]), data.value(), assigned};
    }

    expected_void_t encode_ipv6_connect_response_packet(const span_uint8_t dest, const ipv6_connect_response_frame& response) noexcept
    {
        constexpr auto total_length = frame::communication_header_size + ipv6_connect_response_body_size;
        if (dest.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));
        if (response.data_endpoint.protocol != internal::udp_protocol)
            return std::unexpected(make_error_code(error::unsupported_hpai));
        if (!internal::valid_status(static_cast<std::uint8_t>(response.status)))
            return std::unexpected(make_error_code(error::invalid_configuration));

        const auto header = frame::encode_communication_header(dest, connect_response_service,
                                                               static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());
        dest[6u] = response.channel_id;
        dest[7u] = static_cast<std::uint8_t>(response.status);
        internal::encode_ipv6_hpai({dest.data() + 8u, ipv6_hpai_size}, response.data_endpoint);
        dest[28u] = connection_information_size;
        dest[29u] = internal::tunnelling_type;
        dest[30u] = static_cast<std::uint8_t>(response.assigned_address.value() >> 8u);
        dest[31u] = static_cast<std::uint8_t>(response.assigned_address.value() & 0xFFu);
        return {};
    }

    std::expected<ipv6_connect_response_frame, std::error_code> decode_ipv6_connect_response_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if (!internal::valid_header(header.value(), packet.size(), connect_response_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        if (packet.size() != frame::communication_header_size + ipv6_connect_response_body_size)
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto data = internal::decode_ipv6_hpai({packet.data() + 8u, ipv6_hpai_size});
        if (!data.has_value())
            return std::unexpected(data.error());
        if ((packet[28u] != connection_information_size) || (packet[29u] != internal::tunnelling_type))
            return std::unexpected(make_error_code(error::unsupported_connection_type));
        if (!internal::valid_status(packet[7u]))
            return std::unexpected(make_error_code(error::malformed_frame));

        const individual_address assigned {static_cast<std::uint16_t>((static_cast<std::uint16_t>(packet[30u]) << 8u) | packet[31u])};
        return ipv6_connect_response_frame {packet[6u], static_cast<connect_status>(packet[7u]), data.value(), assigned};
    }

    expected_void_t encode_connectionstate_request_packet(const span_uint8_t dest, const connectionstate_request_frame& request) noexcept
    {
        return internal::encode_control_packet(dest, connectionstate_request_service, request.channel_id, 0u);
    }

    std::expected<connectionstate_request_frame, std::error_code> decode_connectionstate_request_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto fields = internal::decode_control_packet(packet, connectionstate_request_service);
        if (!fields.has_value())
            return std::unexpected(fields.error());
        if (fields->at(1u) != 0u)
            return std::unexpected(make_error_code(error::malformed_frame));

        return connectionstate_request_frame { fields->at(0u) };
    }

    expected_void_t encode_connectionstate_response_packet(
        const span_uint8_t dest, const connectionstate_response_frame& response) noexcept
    {
        if (!internal::valid_status(static_cast<std::uint8_t>(response.status)))
            return std::unexpected(make_error_code(error::invalid_configuration));

        return internal::encode_control_packet(dest,
                                     connectionstate_response_service,
                                     response.channel_id,
                                     static_cast<std::uint8_t>(response.status));
    }

    std::expected<connectionstate_response_frame, std::error_code> decode_connectionstate_response_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto fields = internal::decode_control_packet(packet, connectionstate_response_service);
        if (!fields.has_value())
            return std::unexpected(fields.error());

        if (!internal::valid_status(fields->at(1u)))
            return std::unexpected(make_error_code(error::malformed_frame));

        return connectionstate_response_frame { fields->at(0u), static_cast<connect_status>(fields->at(1u)) };
    }

    expected_void_t encode_disconnect_request_packet(const span_uint8_t dest, const disconnect_request_frame& request) noexcept
    {
        return internal::encode_control_packet(dest, disconnect_request_service, request.channel_id, 0u);
    }

    std::expected<disconnect_request_frame, std::error_code> decode_disconnect_request_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto fields = internal::decode_control_packet(packet, disconnect_request_service);
        if (!fields.has_value())
            return std::unexpected(fields.error());
        if (fields->at(1u) != 0u)
            return std::unexpected(make_error_code(error::malformed_frame));

        return disconnect_request_frame { fields->at(0u) };
    }

    expected_void_t encode_disconnect_response_packet(const span_uint8_t dest, const disconnect_response_frame& response) noexcept
    {
        if (!internal::valid_status(static_cast<std::uint8_t>(response.status)))
            return std::unexpected(make_error_code(error::invalid_configuration));

        return internal::encode_control_packet(dest,
                         disconnect_response_service,
                                     response.channel_id,
                                     static_cast<std::uint8_t>(response.status));
    }

    std::expected<disconnect_response_frame, std::error_code> decode_disconnect_response_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto fields = internal::decode_control_packet(packet, disconnect_response_service);
        if (!fields.has_value())
            return std::unexpected(fields.error());

        if (!internal::valid_status(fields->at(1u)))
            return std::unexpected(make_error_code(error::malformed_frame));

        return disconnect_response_frame { fields->at(0u), static_cast<connect_status>(fields->at(1u)) };
    }
}
