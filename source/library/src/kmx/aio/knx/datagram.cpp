/// @file src/kmx/aio/knx/datagram.cpp
/// @brief Service-type dispatch that decodes and encodes every supported KNXnet/IP datagram.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/datagram.hpp>
#ifndef PCH
    #include <optional>
#endif

namespace kmx::aio::knx
{
    /// @brief A decode outcome, or nothing when the service belongs to another group.
    using optional_datagram_result_t = std::optional<datagram_result_t>;
    /// @brief An encode outcome, or nothing when the service belongs to another group.
    using optional_expected_void_t = std::optional<expected_void_t>;

    /// @brief Wraps one body decoder's result as a datagram.
    /// @tparam Decoder The body decoder's type.
    /// @param service The service type the header named.
    /// @param packet The datagram to decode.
    /// @param decode The decoder for that service's body.
    /// @return The datagram, or why the body could not be read.
    /// @details Every arm of the dispatch below is the same three steps - decode, propagate the error,
    ///          wrap - so they are written once here and each arm names only its own decoder.
    template <typename Decoder>
    [[nodiscard]] static datagram_result_t as_datagram(const std::uint16_t service, const cspan_uint8_t packet, Decoder&& decode) noexcept
    {
        const auto decoded = decode(packet);
        if (!decoded.has_value())
            return std::unexpected(decoded.error());
        return datagram {service, decoded.value()};
    }

    /// @brief Decodes a CONNECT_REQUEST, whose IPv6 and management forms share one service type.
    [[nodiscard]] static datagram_result_t decode_connect_request(const std::uint16_t service, const cspan_uint8_t packet,
                                                                  const std::uint16_t total_length) noexcept
    {
        if (total_length == frame::communication_header_size + connection::ipv6_connect_request_body_size)
            return as_datagram(service, packet, connection::decode_ipv6_connect_request_packet);
        // Attempted rather than required: a body of this length may still be an ordinary request, so a
        // failure here falls through to the tunnelling form instead of rejecting the datagram.
        if (total_length == frame::communication_header_size + connection::management_connect_request_body_size)
            if (const auto decoded = connection::decode_management_connect_request_packet(packet); decoded.has_value())
                return datagram {service, decoded.value()};
        return as_datagram(service, packet, connection::decode_connect_request_packet);
    }

    /// @brief Decodes a CONNECT_RESPONSE, whose IPv6 and management forms share one service type.
    [[nodiscard]] static datagram_result_t decode_connect_response(const std::uint16_t service, const cspan_uint8_t packet,
                                                                   const std::uint16_t total_length) noexcept
    {
        if (total_length == frame::communication_header_size + connection::ipv6_connect_response_body_size)
            return as_datagram(service, packet, connection::decode_ipv6_connect_response_packet);
        if (total_length == frame::communication_header_size + connection::management_connect_response_body_size)
            if (const auto decoded = connection::decode_management_connect_response_packet(packet); decoded.has_value())
                return datagram {service, decoded.value()};
        return as_datagram(service, packet, connection::decode_connect_response_packet);
    }

    /// @brief Decodes the connectionless discovery services.
    [[nodiscard]] static optional_datagram_result_t decode_discovery_service(const std::uint16_t service, const cspan_uint8_t packet,
                                                                             const std::uint16_t total_length) noexcept
    {
        switch (service)
        {
            case discovery::search_request_service:
                // The IPv6 form is the same service with a longer body; the length is what separates them.
                if (total_length == frame::communication_header_size + discovery::ipv6_search_request_body_size)
                    return as_datagram(service, packet, discovery::decode_ipv6_search_request_packet);
                return as_datagram(service, packet, discovery::decode_search_request_packet);
            case discovery::search_response_service:
                // Tried as IPv6 and fallen back, because a body this long may equally be the IPv4 form
                // with a large description behind it.
                if (total_length >= frame::communication_header_size + connection::ipv6_hpai_size + 2u)
                    if (const auto decoded = discovery::decode_ipv6_search_response_packet(packet); decoded.has_value())
                        return datagram_result_t {datagram {service, decoded.value()}};
                return as_datagram(service, packet, discovery::decode_search_response_packet);
            case discovery::search_request_extended_service:
                return as_datagram(service, packet, discovery::decode_extended_search_request_packet);
            case discovery::search_response_extended_service:
                return as_datagram(service, packet, discovery::decode_extended_search_response_packet);
            case discovery::description_request_service:
                return as_datagram(service, packet, discovery::decode_description_request_packet);
            case discovery::description_response_service:
                return as_datagram(service, packet, discovery::decode_description_response_packet);
            default:
                return {};
        }
    }

    /// @brief Decodes the channel management services.
    [[nodiscard]] static optional_datagram_result_t decode_connection_service(const std::uint16_t service, const cspan_uint8_t packet,
                                                                              const std::uint16_t total_length) noexcept
    {
        switch (service)
        {
            case connection::connect_request_service:
                return decode_connect_request(service, packet, total_length);
            case connection::connect_response_service:
                return decode_connect_response(service, packet, total_length);
            case connection::connectionstate_request_service:
                return as_datagram(service, packet, connection::decode_connectionstate_request_packet);
            case connection::connectionstate_response_service:
                return as_datagram(service, packet, connection::decode_connectionstate_response_packet);
            case connection::disconnect_request_service:
                return as_datagram(service, packet, connection::decode_disconnect_request_packet);
            case connection::disconnect_response_service:
                return as_datagram(service, packet, connection::decode_disconnect_response_packet);
            default:
                return {};
        }
    }

    /// @brief Decodes the services carried on an established tunnelling channel.
    [[nodiscard]] static optional_datagram_result_t decode_tunnelling_service(const std::uint16_t service,
                                                                              const cspan_uint8_t packet) noexcept
    {
        switch (service)
        {
            case frame::tunnelling_request_service:
                return as_datagram(service, packet, frame::decode_tunnelling_request_packet);
            case frame::tunnelling_ack_service:
                return as_datagram(service, packet, frame::decode_tunnelling_ack_packet);
            case frame::device_configuration_request_service:
                return as_datagram(service, packet, frame::decode_device_configuration_request_packet);
            case frame::device_configuration_ack_service:
                return as_datagram(service, packet, frame::decode_device_configuration_ack_packet);
            case frame::tunnelling_feature_get_service:
            case frame::tunnelling_feature_response_service:
            case frame::tunnelling_feature_set_service:
            case frame::tunnelling_feature_info_service:
                return as_datagram(service, packet, frame::decode_tunnelling_feature_packet);
            default:
                return {};
        }
    }

    /// @brief Decodes the connectionless routing services.
    [[nodiscard]] static optional_datagram_result_t decode_routing_service(const std::uint16_t service, const cspan_uint8_t packet) noexcept
    {
        switch (service)
        {
            case routing::indication_service:
                return as_datagram(service, packet, routing::decode_indication_packet);
            case routing::lost_message_service:
                return as_datagram(service, packet, routing::decode_lost_message_packet);
            case routing::busy_service:
                return as_datagram(service, packet, routing::decode_busy_packet);
            default:
                return {};
        }
    }

    /// @brief Decodes the KNX IP Secure services: SECURE_WRAPPER, the four session services, and TIMER_NOTIFY.
    [[nodiscard]] static optional_datagram_result_t decode_secure_service(const std::uint16_t service, const cspan_uint8_t packet) noexcept
    {
        switch (service)
        {
            case secure::wrapper_service:
                return as_datagram(service, packet, secure::decode_wrapper_packet);
            case secure::session_request_service:
                return as_datagram(service, packet, secure::decode_session_request_packet);
            case secure::session_response_service:
                return as_datagram(service, packet, secure::decode_session_response_packet);
            case secure::session_authenticate_service:
                return as_datagram(service, packet, secure::decode_session_authenticate_packet);
            case secure::session_status_service:
                return as_datagram(service, packet, secure::decode_session_status_packet);
            case secure::timer_notify_service:
                return as_datagram(service, packet, secure::decode_timer_notify_packet);
            default:
                return {};
        }
    }

    datagram_result_t decode_datagram(const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if (header->protocol_version != 0x10u)
            return std::unexpected(make_error_code(error::unsupported_service));
        if (header->total_length != packet.size())
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto service = header->service_type;
        const auto length = header->total_length;
        if (const auto decoded = decode_discovery_service(service, packet, length); decoded.has_value())
            return *decoded;
        if (const auto decoded = decode_connection_service(service, packet, length); decoded.has_value())
            return *decoded;
        if (const auto decoded = decode_tunnelling_service(service, packet); decoded.has_value())
            return *decoded;
        if (const auto decoded = decode_routing_service(service, packet); decoded.has_value())
            return *decoded;
        if (const auto decoded = decode_secure_service(service, packet); decoded.has_value())
            return *decoded;
        return std::unexpected(make_error_code(error::unsupported_service));
    }

    /// @brief Encodes the payload a service expects, or reports that the datagram does not carry it.
    /// @tparam Payload The frame type this service encodes.
    /// @tparam Encoder The encoder's type.
    /// @param packet The destination octets.
    /// @param value The datagram whose payload is to be encoded.
    /// @param encode The encoder for @p Payload.
    /// @return Nothing, or why the datagram could not be encoded.
    /// @details Every arm below asks the same question - is the payload the one this service names - so
    ///          the check and the error it reports live here rather than once per service.
    template <typename Payload, typename Encoder>
    [[nodiscard]] static expected_void_t encode_payload(const span_uint8_t packet, const datagram& value, Encoder&& encode) noexcept
    {
        const auto* payload = std::get_if<Payload>(&value.payload);
        if (payload == nullptr)
            return std::unexpected(make_error_code(error::invalid_configuration));
        return encode(packet, *payload);
    }

    /// @brief Encodes a SEARCH_REQUEST in whichever address family the payload holds.
    [[nodiscard]] static expected_void_t encode_search_request(const span_uint8_t packet, const datagram& value) noexcept
    {
        if (const auto* request = std::get_if<discovery::ipv6_search_request_frame>(&value.payload))
            return discovery::encode_ipv6_search_request_packet(packet, *request);
        return encode_payload<discovery::search_request_frame>(packet, value, discovery::encode_search_request_packet);
    }

    /// @brief Encodes a SEARCH_RESPONSE in whichever address family the payload holds.
    [[nodiscard]] static expected_void_t encode_search_response(const span_uint8_t packet, const datagram& value) noexcept
    {
        if (const auto* response = std::get_if<discovery::ipv6_search_response_frame>(&value.payload))
            return discovery::encode_ipv6_search_response_packet(packet, *response);
        return encode_payload<discovery::search_response_frame>(packet, value, discovery::encode_search_response_packet);
    }

    /// @brief Encodes a CONNECT_REQUEST in whichever of the three forms the payload holds.
    [[nodiscard]] static expected_void_t encode_connect_request(const span_uint8_t packet, const datagram& value) noexcept
    {
        if (const auto* request = std::get_if<ipv6_connect_request_frame>(&value.payload))
            return connection::encode_ipv6_connect_request_packet(packet, *request);
        if (const auto* request = std::get_if<management_connect_request_frame>(&value.payload))
            return connection::encode_management_connect_request_packet(packet, *request);
        return encode_payload<connect_request_frame>(packet, value, connection::encode_connect_request_packet);
    }

    /// @brief Encodes a CONNECT_RESPONSE in whichever of the three forms the payload holds.
    [[nodiscard]] static expected_void_t encode_connect_response(const span_uint8_t packet, const datagram& value) noexcept
    {
        if (const auto* response = std::get_if<ipv6_connect_response_frame>(&value.payload))
            return connection::encode_ipv6_connect_response_packet(packet, *response);
        if (const auto* response = std::get_if<management_connect_response_frame>(&value.payload))
            return connection::encode_management_connect_response_packet(packet, *response);
        return encode_payload<connect_response_frame>(packet, value, connection::encode_connect_response_packet);
    }

    /// @brief Encodes a TUNNELLING_REQUEST, rejecting one that carries no cEMI to tunnel.
    [[nodiscard]] static expected_void_t encode_tunnelling(const span_uint8_t packet, const tunnelling_request_frame& request) noexcept
    {
        if (request.cemi_bytes.empty())
            return std::unexpected(make_error_code(error::malformed_frame));
        return frame::encode_tunnelling_request_packet(packet, request.channel_id, request.sequence_number, request.cemi_bytes.span());
    }

    /// @brief Encodes a TUNNELLING_ACK from the fields the frame carries.
    [[nodiscard]] static expected_void_t encode_ack(const span_uint8_t packet, const tunnelling_ack_frame& ack) noexcept
    {
        return frame::encode_tunnelling_ack_packet(packet, ack.channel_id, ack.sequence_number, ack.status);
    }

    /// @brief Encodes a tunnelling feature service, whose value travels beside the frame that names it.
    [[nodiscard]] static expected_void_t encode_feature(const span_uint8_t packet, const tunnelling_feature_frame& feature) noexcept
    {
        return frame::encode_tunnelling_feature_packet(packet, feature, feature.value.span());
    }

    /// @brief Encodes the connectionless discovery services.
    [[nodiscard]] static optional_expected_void_t encode_discovery_service(const std::uint16_t service, const span_uint8_t packet,
                                                                           const datagram& value) noexcept
    {
        switch (service)
        {
            case discovery::search_request_service:
                return encode_search_request(packet, value);
            case discovery::search_response_service:
                return encode_search_response(packet, value);
            case discovery::search_request_extended_service:
                return encode_payload<discovery::extended_search_request_frame>(packet, value,
                                                                                discovery::encode_extended_search_request_packet);
            case discovery::search_response_extended_service:
                return encode_payload<discovery::extended_search_response_frame>(packet, value,
                                                                                 discovery::encode_extended_search_response_packet);
            case discovery::description_request_service:
                return encode_payload<discovery::description_request_frame>(packet, value, discovery::encode_description_request_packet);
            case discovery::description_response_service:
                return encode_payload<discovery::description_response_frame>(packet, value, discovery::encode_description_response_packet);
            default:
                return {};
        }
    }

    /// @brief Encodes the channel management services.
    [[nodiscard]] static optional_expected_void_t encode_connection_service(const std::uint16_t service, const span_uint8_t packet,
                                                                            const datagram& value) noexcept
    {
        switch (service)
        {
            case connection::connect_request_service:
                return encode_connect_request(packet, value);
            case connection::connect_response_service:
                return encode_connect_response(packet, value);
            case connection::connectionstate_request_service:
                return encode_payload<connectionstate_request_frame>(packet, value, connection::encode_connectionstate_request_packet);
            case connection::connectionstate_response_service:
                return encode_payload<connectionstate_response_frame>(packet, value, connection::encode_connectionstate_response_packet);
            case connection::disconnect_request_service:
                return encode_payload<disconnect_request_frame>(packet, value, connection::encode_disconnect_request_packet);
            case connection::disconnect_response_service:
                return encode_payload<disconnect_response_frame>(packet, value, connection::encode_disconnect_response_packet);
            default:
                return {};
        }
    }

    /// @brief Encodes the services carried on an established tunnelling channel.
    [[nodiscard]] static optional_expected_void_t encode_tunnelling_service(const std::uint16_t service, const span_uint8_t packet,
                                                                            const datagram& value) noexcept
    {
        switch (service)
        {
            case frame::tunnelling_request_service:
                return encode_payload<tunnelling_request_frame>(packet, value, encode_tunnelling);
            case frame::tunnelling_ack_service:
                return encode_payload<tunnelling_ack_frame>(packet, value, encode_ack);
            case frame::tunnelling_feature_get_service:
            case frame::tunnelling_feature_response_service:
            case frame::tunnelling_feature_set_service:
            case frame::tunnelling_feature_info_service:
                return encode_payload<tunnelling_feature_frame>(packet, value, encode_feature);
            default:
                return {};
        }
    }

    /// @brief Encodes the connectionless routing services.
    [[nodiscard]] static optional_expected_void_t encode_routing_service(const std::uint16_t service, const span_uint8_t packet,
                                                                         const datagram& value) noexcept
    {
        switch (service)
        {
            case routing::indication_service:
                return encode_payload<routing::indication>(packet, value, routing::encode_indication_packet);
            case routing::lost_message_service:
                return encode_payload<routing::lost_message>(packet, value, routing::encode_lost_message_packet);
            case routing::busy_service:
                return encode_payload<routing::busy>(packet, value, routing::encode_busy_packet);
            default:
                return {};
        }
    }

    /// @brief Encodes the KNX IP Secure services this build decodes.
    [[nodiscard]] static optional_expected_void_t encode_secure_service(const std::uint16_t service, const span_uint8_t packet,
                                                                        const datagram& value) noexcept
    {
        switch (service)
        {
            case secure::wrapper_service:
                return encode_payload<secure::wrapper_frame>(packet, value, secure::encode_wrapper_packet);
            case secure::session_request_service:
                return encode_payload<secure::session_request_frame>(packet, value, secure::encode_session_request_packet);
            case secure::session_response_service:
                return encode_payload<secure::session_response_frame>(packet, value, secure::encode_session_response_packet);
            case secure::session_authenticate_service:
                return encode_payload<secure::session_authenticate_frame>(packet, value, secure::encode_session_authenticate_packet);
            case secure::session_status_service:
                return encode_payload<secure::session_status_frame>(packet, value, secure::encode_session_status_packet);
            case secure::timer_notify_service:
                return encode_payload<secure::timer_notify_frame>(packet, value, secure::encode_timer_notify_packet);
            default:
                return {};
        }
    }

    expected_void_t encode_datagram(const span_uint8_t packet, const datagram& value) noexcept
    {
        const auto service = value.service_type;
        if (const auto encoded = encode_secure_service(service, packet, value); encoded.has_value())
            return *encoded;
        if (const auto encoded = encode_discovery_service(service, packet, value); encoded.has_value())
            return *encoded;
        if (const auto encoded = encode_connection_service(service, packet, value); encoded.has_value())
            return *encoded;
        if (const auto encoded = encode_tunnelling_service(service, packet, value); encoded.has_value())
            return *encoded;
        if (const auto encoded = encode_routing_service(service, packet, value); encoded.has_value())
            return *encoded;
        return std::unexpected(make_error_code(error::unsupported_service));
    }

    expected_void_t encode_response_datagram(const span_uint8_t packet, const datagram& request, const std::uint8_t status) noexcept
    {
        if (const auto* tunnel = std::get_if<tunnelling_request_frame>(&request.payload))
        {
            if (request.service_type != frame::tunnelling_request_service)
                return std::unexpected(make_error_code(error::invalid_configuration));
            return frame::encode_tunnelling_ack_packet(packet, tunnel->channel_id, tunnel->sequence_number, status);
        }

        if (const auto* heartbeat = std::get_if<connectionstate_request_frame>(&request.payload))
        {
            if (request.service_type != connection::connectionstate_request_service)
                return std::unexpected(make_error_code(error::invalid_configuration));
            return connection::encode_connectionstate_response_packet(
                packet, connectionstate_response_frame {heartbeat->channel_id, static_cast<connect_status>(status)});
        }

        if (const auto* disconnect = std::get_if<disconnect_request_frame>(&request.payload))
        {
            if (request.service_type != connection::disconnect_request_service)
                return std::unexpected(make_error_code(error::invalid_configuration));
            return connection::encode_disconnect_response_packet(
                packet, disconnect_response_frame {disconnect->channel_id, static_cast<connect_status>(status)});
        }

        return std::unexpected(make_error_code(error::unsupported_service));
    }
}
