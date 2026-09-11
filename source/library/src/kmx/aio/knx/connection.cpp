/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/connection.hpp>

namespace kmx::aio::knx::connection
{
    namespace internal
    {
        constexpr std::uint8_t udp_protocol = 0x01u;
        constexpr std::uint8_t tcp_protocol = 0x02u;
        constexpr std::uint8_t tunnelling_type = tunnel_connection_type;

        /// @brief Indicates whether an HPAI names a host protocol KNXnet/IP defines.
        /// @details The decoders accept both, because which one a peer may use is the endpoint's decision and
        ///          not the codec's: a UDP server refuses a TCP HPAI with E_HOST_PROTOCOL_TYPE, which it can
        ///          only do once the request has been read rather than dropped as unreadable.
        constexpr bool known_protocol(const std::uint8_t value) noexcept
        {
            return (value == udp_protocol) || (value == tcp_protocol);
        }
        constexpr std::uint8_t link_layer = tunnel_link_layer;

        bool valid_status(const std::uint8_t value) noexcept
        {
            switch (static_cast<connect_status>(value))
            {
                case connect_status::no_error:
                case connect_status::host_protocol_type:
                case connect_status::version_not_supported:
                case connect_status::sequence_number:
                case connect_status::connection_id:
                case connect_status::connection_type:
                case connect_status::connection_option:
                case connect_status::no_more_connections:
                case connect_status::no_more_unique_connections:
                case connect_status::data_connection:
                case connect_status::knx_connection:
                case connect_status::authorisation_error:
                case connect_status::tunnelling_layer:
                case connect_status::no_tunnelling_address:
                case connect_status::connection_in_use:
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
            if (!known_protocol(source[1]))
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
            if (!known_protocol(source[1]))
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

        /// @brief Encodes a CONNECTIONSTATE_REQUEST or a DISCONNECT_REQUEST: channel, reserved octet, control endpoint.
        expected_void_t encode_control_request_packet(const span_uint8_t dest, const std::uint16_t service, const std::uint8_t channel_id,
                                                      const hpai& control_endpoint) noexcept
        {
            constexpr std::size_t total_length = frame::communication_header_size + control_request_body_size;
            if (dest.size() < total_length)
                return std::unexpected(make_error_code(error::invalid_length));
            if (!known_protocol(control_endpoint.protocol))
                return std::unexpected(make_error_code(error::unsupported_hpai));

            const auto header = frame::encode_communication_header(dest, service, static_cast<std::uint16_t>(total_length));
            if (!header.has_value())
                return std::unexpected(header.error());
            dest[6u] = channel_id;
            dest[7u] = 0x00u;
            encode_hpai({dest.data() + 8u, hpai_size}, control_endpoint);
            return {};
        }

        /// @brief The channel a control request names, and the control endpoint it carries.
        struct control_request_fields
        {
            std::uint8_t channel_id {};
            hpai control_endpoint {};
        };

        /// @brief Decodes a CONNECTIONSTATE_REQUEST or a DISCONNECT_REQUEST.
        std::expected<control_request_fields, std::error_code> decode_control_request_packet(const cspan_uint8_t packet,
                                                                                          const std::uint16_t service) noexcept
        {
            const auto header = frame::decode_communication_header(packet);
            if (!header.has_value())
                return std::unexpected(header.error());
            if (!valid_header(header.value(), packet.size(), service))
                return std::unexpected(make_error_code(error::unsupported_service));
            // The control endpoint is part of the request: an eight-octet form without it is not one a peer sends.
            if ((packet.size() != frame::communication_header_size + control_request_body_size) || (packet[7u] != 0x00u))
                return std::unexpected(make_error_code(error::malformed_frame));

            const auto control = decode_hpai({packet.data() + 8u, hpai_size});
            if (!control.has_value())
                return std::unexpected(control.error());
            return control_request_fields {packet[6u], control.value()};
        }
    } // namespace internal

    /// @brief Writes a tunnelling connection request information block: four octets, or six with a requested address.
    static void encode_connection_request_information(const span_uint8_t dest, const connect_request_frame& request) noexcept
    {
        const auto extended = request.requested_address.has_value();
        dest[0u] = extended ? extended_connection_information_size : connection_information_size;
        dest[1u] = internal::tunnelling_type;
        dest[2u] = request.knx_layer;
        dest[3u] = 0x00u;
        if (!extended)
            return;
        dest[4u] = static_cast<std::uint8_t>(request.requested_address->value() >> 8u);
        dest[5u] = static_cast<std::uint8_t>(request.requested_address->value() & 0xFFu);
    }

    expected_void_t encode_connect_request_packet(const span_uint8_t dest, const connect_request_frame& request) noexcept
    {
        const auto body_size = request.requested_address.has_value() ? extended_connect_request_body_size : connect_request_body_size;
        const auto total_length = frame::communication_header_size + body_size;
        if (dest.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));
        // Both endpoints use the same host protocol: UDP, or the TCP connection the request travels on.
        if (!internal::known_protocol(request.control_endpoint.protocol) ||
            (request.data_endpoint.protocol != request.control_endpoint.protocol))
            return std::unexpected(make_error_code(error::unsupported_hpai));

        const auto header = frame::encode_communication_header(dest, connect_request_service,
                                                               static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        if (!valid_tunnel_layer(request.knx_layer))
            return std::unexpected(make_error_code(error::invalid_configuration));

        internal::encode_hpai({ dest.data() + 6u, hpai_size }, request.control_endpoint);
        internal::encode_hpai({ dest.data() + 14u, hpai_size }, request.data_endpoint);
        encode_connection_request_information(dest.subspan(22u), request);
        return {};
    }

    std::expected<connect_request_frame, std::error_code> decode_connect_request_packet(const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if (!internal::valid_header(header.value(), packet.size(), connect_request_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        const auto extended = packet.size() == (frame::communication_header_size + extended_connect_request_body_size);
        if (!extended && (packet.size() != frame::communication_header_size + connect_request_body_size))
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto control = internal::decode_hpai({ packet.data() + 6u, hpai_size });
        if (!control.has_value())
            return std::unexpected(control.error());
        const auto data = internal::decode_hpai({packet.data() + 14u, hpai_size});
        if (!data.has_value())
            return std::unexpected(data.error());
        const auto information_size = extended ? extended_connection_information_size : connection_information_size;
        if ((packet[22u] != information_size) || (packet[23u] != internal::tunnelling_type) || !valid_tunnel_layer(packet[24u]) ||
            (packet[25u] != 0x00u))
            return std::unexpected(make_error_code(error::unsupported_connection_type));

        connect_request_frame value {control.value(), data.value(), packet[24u]};
        if (extended)
            value.requested_address = individual_address {static_cast<std::uint16_t>((packet[26u] << 8u) | packet[27u])};
        return value;
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
        if (!internal::known_protocol(response.data_endpoint.protocol))
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

        // When a server rejects a connection, KNXnet/IP allows an 8-octet response: communication header,
        // channel 0, and the error status, omitting HPAI and CRD.
        if (packet.size() == (frame::communication_header_size + 2u))
        {
            if (!internal::valid_status(packet[7u]))
                return std::unexpected(make_error_code(error::malformed_frame));
            const auto status = static_cast<connect_status>(packet[7u]);
            if (status == connect_status::no_error)
                return std::unexpected(make_error_code(error::malformed_frame));
            return connect_response_frame {packet[6u], status, {}, {}};
        }

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
        return internal::encode_control_request_packet(dest, connectionstate_request_service, request.channel_id, request.control_endpoint);
    }

    std::expected<connectionstate_request_frame, std::error_code> decode_connectionstate_request_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto fields = internal::decode_control_request_packet(packet, connectionstate_request_service);
        if (!fields.has_value())
            return std::unexpected(fields.error());

        return connectionstate_request_frame {fields->channel_id, fields->control_endpoint};
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
        return internal::encode_control_request_packet(dest, disconnect_request_service, request.channel_id, request.control_endpoint);
    }

    std::expected<disconnect_request_frame, std::error_code> decode_disconnect_request_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto fields = internal::decode_control_request_packet(packet, disconnect_request_service);
        if (!fields.has_value())
            return std::unexpected(fields.error());

        return disconnect_request_frame {fields->channel_id, fields->control_endpoint};
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

    expected_void_t encode_management_connect_request_packet(const span_uint8_t dest,
                                                             const management_connect_request_frame& request) noexcept
    {
        constexpr auto total_length = frame::communication_header_size + management_connect_request_body_size;
        if (dest.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));
        if ((request.control_endpoint.protocol != internal::udp_protocol) || (request.data_endpoint.protocol != internal::udp_protocol))
            return std::unexpected(make_error_code(error::unsupported_hpai));

        const auto header =
            frame::encode_communication_header(dest, connect_request_service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        internal::encode_hpai({dest.data() + 6u, hpai_size}, request.control_endpoint);
        internal::encode_hpai({dest.data() + 14u, hpai_size}, request.data_endpoint);
        // A management connection information block is two octets: its own length and the type. There is no
        // KNX layer, because nothing is tunnelled onto the bus.
        dest[22u] = management_information_size;
        dest[23u] = management_connection_type;
        return {};
    }

    std::expected<management_connect_request_frame, std::error_code> decode_management_connect_request_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if (!internal::valid_header(header.value(), packet.size(), connect_request_service))
            return std::unexpected(make_error_code(error::unsupported_service));
        if (packet.size() != frame::communication_header_size + management_connect_request_body_size)
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto control = internal::decode_hpai({packet.data() + 6u, hpai_size});
        if (!control.has_value())
            return std::unexpected(control.error());
        const auto data = internal::decode_hpai({packet.data() + 14u, hpai_size});
        if (!data.has_value())
            return std::unexpected(data.error());
        if ((packet[22u] != management_information_size) || (packet[23u] != management_connection_type))
            return std::unexpected(make_error_code(error::unsupported_connection_type));

        return management_connect_request_frame {control.value(), data.value()};
    }

    expected_void_t encode_management_connect_response_packet(const span_uint8_t dest,
                                                              const management_connect_response_frame& response) noexcept
    {
        constexpr auto total_length = frame::communication_header_size + management_connect_response_body_size;
        if (dest.size() < total_length)
            return std::unexpected(make_error_code(error::invalid_length));
        if (response.data_endpoint.protocol != internal::udp_protocol)
            return std::unexpected(make_error_code(error::unsupported_hpai));

        const auto header =
            frame::encode_communication_header(dest, connect_response_service, static_cast<std::uint16_t>(total_length));
        if (!header.has_value())
            return std::unexpected(header.error());

        dest[6u] = response.channel_id;
        dest[7u] = static_cast<std::uint8_t>(response.status);
        internal::encode_hpai({dest.data() + 8u, hpai_size}, response.data_endpoint);
        dest[16u] = management_information_size;
        dest[17u] = management_connection_type;
        return {};
    }

    std::expected<management_connect_response_frame, std::error_code> decode_management_connect_response_packet(
        const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if (!internal::valid_header(header.value(), packet.size(), connect_response_service))
            return std::unexpected(make_error_code(error::unsupported_service));

        if (packet.size() == (frame::communication_header_size + 2u))
        {
            if (!internal::valid_status(packet[7u]))
                return std::unexpected(make_error_code(error::malformed_frame));
            const auto status = static_cast<connect_status>(packet[7u]);
            if (status == connect_status::no_error)
                return std::unexpected(make_error_code(error::malformed_frame));
            return management_connect_response_frame {packet[6u], status, {}};
        }

        if (packet.size() != frame::communication_header_size + management_connect_response_body_size)
            return std::unexpected(make_error_code(error::malformed_frame));
        if (!internal::valid_status(packet[7u]))
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto data = internal::decode_hpai({packet.data() + 8u, hpai_size});
        if (!data.has_value())
            return std::unexpected(data.error());
        if ((packet[16u] != management_information_size) || (packet[17u] != management_connection_type))
            return std::unexpected(make_error_code(error::unsupported_connection_type));

        return management_connect_response_frame {packet[6u], static_cast<connect_status>(packet[7u]), data.value()};
    }
}
