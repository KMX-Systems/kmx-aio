/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/datagram.hpp>

namespace kmx::aio::knx
{
    std::expected<datagram, std::error_code> decode_datagram(const cspan_uint8_t packet) noexcept
    {
        const auto header = frame::decode_communication_header(packet);
        if (!header.has_value())
            return std::unexpected(header.error());
        if (header->protocol_version != 0x10u)
            return std::unexpected(make_error_code(error::unsupported_service));
        if (header->total_length != packet.size())
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto service = header->service_type;
        switch (service)
        {
            case discovery::search_request_service:
            {
                if (header->total_length == frame::communication_header_size + discovery::ipv6_search_request_body_size)
                {
                    const auto decoded = discovery::decode_ipv6_search_request_packet(packet);
                    if (!decoded.has_value())
                        return std::unexpected(decoded.error());
                    return datagram { service, decoded.value() };
                }
                const auto decoded = discovery::decode_search_request_packet(packet);
                if (!decoded.has_value())
                    return std::unexpected(decoded.error());
                return datagram { service, decoded.value() };
            }
            case discovery::search_response_service:
            {
                if (header->total_length >= frame::communication_header_size + connection::ipv6_hpai_size + 2u)
                {
                    const auto decoded = discovery::decode_ipv6_search_response_packet(packet);
                    if (decoded.has_value())
                        return datagram { service, decoded.value() };
                }
                const auto decoded = discovery::decode_search_response_packet(packet);
                if (!decoded.has_value())
                    return std::unexpected(decoded.error());
                return datagram { service, decoded.value() };
            }
            case discovery::description_request_service:
            {
                const auto decoded = discovery::decode_description_request_packet(packet);
                if (!decoded.has_value())
                    return std::unexpected(decoded.error());
                return datagram { service, decoded.value() };
            }
            case discovery::description_response_service:
            {
                const auto decoded = discovery::decode_description_response_packet(packet);
                if (!decoded.has_value())
                    return std::unexpected(decoded.error());
                return datagram { service, decoded.value() };
            }
            case connection::connect_request_service:
            {
                if (header->total_length == frame::communication_header_size + connection::ipv6_connect_request_body_size)
                {
                    const auto decoded = connection::decode_ipv6_connect_request_packet(packet);
                    if (!decoded.has_value())
                        return std::unexpected(decoded.error());
                    return datagram { service, decoded.value() };
                }
                const auto decoded = connection::decode_connect_request_packet(packet);
                if (!decoded.has_value())
                    return std::unexpected(decoded.error());
                return datagram { service, decoded.value() };
            }
            case connection::connect_response_service:
            {
                if (header->total_length == frame::communication_header_size + connection::ipv6_connect_response_body_size)
                {
                    const auto decoded = connection::decode_ipv6_connect_response_packet(packet);
                    if (!decoded.has_value())
                        return std::unexpected(decoded.error());
                    return datagram { service, decoded.value() };
                }
                const auto decoded = connection::decode_connect_response_packet(packet);
                if (!decoded.has_value())
                    return std::unexpected(decoded.error());
                return datagram { service, decoded.value() };
            }
            case connection::connectionstate_request_service:
            {
                const auto decoded = connection::decode_connectionstate_request_packet(packet);
                if (!decoded.has_value())
                    return std::unexpected(decoded.error());
                return datagram { service, decoded.value() };
            }
            case connection::connectionstate_response_service:
            {
                const auto decoded = connection::decode_connectionstate_response_packet(packet);
                if (!decoded.has_value())
                    return std::unexpected(decoded.error());
                return datagram { service, decoded.value() };
            }
            case connection::disconnect_request_service:
            {
                const auto decoded = connection::decode_disconnect_request_packet(packet);
                if (!decoded.has_value())
                    return std::unexpected(decoded.error());
                return datagram { service, decoded.value() };
            }
            case connection::disconnect_response_service:
            {
                const auto decoded = connection::decode_disconnect_response_packet(packet);
                if (!decoded.has_value())
                    return std::unexpected(decoded.error());
                return datagram { service, decoded.value() };
            }
            case routing::indication_service:
            {
                const auto decoded = routing::decode_indication_packet(packet);
                if (!decoded.has_value())
                    return std::unexpected(decoded.error());
                return datagram { service, decoded.value() };
            }
            case routing::lost_message_service:
            {
                const auto decoded = routing::decode_lost_message_packet(packet);
                if (!decoded.has_value())
                    return std::unexpected(decoded.error());
                return datagram { service, decoded.value() };
            }
            case routing::busy_service:
            {
                const auto decoded = routing::decode_busy_packet(packet);
                if (!decoded.has_value())
                    return std::unexpected(decoded.error());
                return datagram { service, decoded.value() };
            }
            case secure::secure_service:
            {
                const auto decoded = secure::decode_secure_packet(packet);
                if (!decoded.has_value())
                    return std::unexpected(decoded.error());
                return datagram { service, decoded.value() };
            }
            case frame::tunnelling_request_service:
            {
                const auto decoded = frame::decode_tunnelling_request_packet(packet);
                if (!decoded.has_value())
                    return std::unexpected(decoded.error());
                return datagram { service, decoded.value() };
            }
            case frame::tunnelling_ack_service:
            {
                const auto decoded = frame::decode_tunnelling_ack_packet(packet);
                if (!decoded.has_value())
                    return std::unexpected(decoded.error());
                return datagram { service, decoded.value() };
            }
            default:
                return std::unexpected(make_error_code(error::unsupported_service));
        }
    }

    std::expected<void, std::error_code> encode_datagram(const span_uint8_t packet, const datagram& value) noexcept
    {
        switch (value.service_type)
        {
            case discovery::search_request_service:
            {
                if (const auto* request = std::get_if<discovery::ipv6_search_request_frame>(&value.payload); request != nullptr)
                    return discovery::encode_ipv6_search_request_packet(packet, *request);
                const auto* request = std::get_if<discovery::search_request_frame>(&value.payload);
                if (request == nullptr)
                    return std::unexpected(make_error_code(error::invalid_configuration));
                return discovery::encode_search_request_packet(packet, *request);
            }
            case discovery::search_response_service:
            {
                if (const auto* response = std::get_if<discovery::ipv6_search_response_frame>(&value.payload); response != nullptr)
                    return discovery::encode_ipv6_search_response_packet(packet, *response);
                const auto* response = std::get_if<discovery::search_response_frame>(&value.payload);
                if (response == nullptr)
                    return std::unexpected(make_error_code(error::invalid_configuration));
                return discovery::encode_search_response_packet(packet, *response);
            }
            case discovery::description_request_service:
                if (std::get_if<discovery::description_request_frame>(&value.payload) == nullptr)
                    return std::unexpected(make_error_code(error::invalid_configuration));
                return discovery::encode_description_request_packet(packet);
            case discovery::description_response_service:
            {
                const auto* response = std::get_if<discovery::description_response_frame>(&value.payload);
                if (response == nullptr)
                    return std::unexpected(make_error_code(error::invalid_configuration));
                return discovery::encode_description_response_packet(packet, *response);
            }
            case connection::connect_request_service:
            {
                if (const auto* request = std::get_if<ipv6_connect_request_frame>(&value.payload); request != nullptr)
                    return connection::encode_ipv6_connect_request_packet(packet, *request);
                const auto* request = std::get_if<connect_request_frame>(&value.payload);
                if (request == nullptr)
                    return std::unexpected(make_error_code(error::invalid_configuration));
                return connection::encode_connect_request_packet(packet, *request);
            }
            case connection::connect_response_service:
            {
                if (const auto* response = std::get_if<ipv6_connect_response_frame>(&value.payload); response != nullptr)
                    return connection::encode_ipv6_connect_response_packet(packet, *response);
                const auto* response = std::get_if<connect_response_frame>(&value.payload);
                if (response == nullptr)
                    return std::unexpected(make_error_code(error::invalid_configuration));
                return connection::encode_connect_response_packet(packet, *response);
            }
            case connection::connectionstate_request_service:
            {
                const auto* request = std::get_if<connectionstate_request_frame>(&value.payload);
                if (request == nullptr)
                    return std::unexpected(make_error_code(error::invalid_configuration));
                return connection::encode_connectionstate_request_packet(packet, *request);
            }
            case connection::connectionstate_response_service:
            {
                const auto* response = std::get_if<connectionstate_response_frame>(&value.payload);
                if (response == nullptr)
                    return std::unexpected(make_error_code(error::invalid_configuration));
                return connection::encode_connectionstate_response_packet(packet, *response);
            }
            case connection::disconnect_request_service:
            {
                const auto* request = std::get_if<disconnect_request_frame>(&value.payload);
                if (request == nullptr)
                    return std::unexpected(make_error_code(error::invalid_configuration));
                return connection::encode_disconnect_request_packet(packet, *request);
            }
            case connection::disconnect_response_service:
            {
                const auto* response = std::get_if<disconnect_response_frame>(&value.payload);
                if (response == nullptr)
                    return std::unexpected(make_error_code(error::invalid_configuration));
                return connection::encode_disconnect_response_packet(packet, *response);
            }
            case routing::indication_service:
            {
                const auto* indication = std::get_if<routing::indication>(&value.payload);
                if (indication == nullptr)
                    return std::unexpected(make_error_code(error::invalid_configuration));
                return routing::encode_indication_packet(packet, *indication);
            }
            case routing::lost_message_service:
            {
                const auto* lost = std::get_if<routing::lost_message>(&value.payload);
                if (lost == nullptr)
                    return std::unexpected(make_error_code(error::invalid_configuration));
                return routing::encode_lost_message_packet(packet, *lost);
            }
            case routing::busy_service:
            {
                const auto* busy = std::get_if<routing::busy>(&value.payload);
                if (busy == nullptr)
                    return std::unexpected(make_error_code(error::invalid_configuration));
                return routing::encode_busy_packet(packet, *busy);
            }
            case secure::secure_service:
            {
                const auto* secure_packet = std::get_if<secure::packet>(&value.payload);
                if (secure_packet == nullptr)
                    return std::unexpected(make_error_code(error::invalid_configuration));
                return secure::encode_secure_packet(packet, *secure_packet);
            }
            case frame::tunnelling_request_service:
            {
                const auto* request = std::get_if<tunnelling_request_frame>(&value.payload);
                if (request == nullptr)
                    return std::unexpected(make_error_code(error::invalid_configuration));
                if (request->cemi_bytes.empty())
                    return std::unexpected(make_error_code(error::malformed_frame));
                return frame::encode_tunnelling_request_packet(packet,
                                                                request->channel_id,
                                                                request->sequence_number,
                                                                   request->cemi_bytes.span());
            }
            case frame::tunnelling_ack_service:
            {
                const auto* ack = std::get_if<tunnelling_ack_frame>(&value.payload);
                if (ack == nullptr)
                    return std::unexpected(make_error_code(error::invalid_configuration));
                return frame::encode_tunnelling_ack_packet(packet,
                                                           ack->channel_id,
                                                           ack->sequence_number,
                                                           ack->status);
            }
            default:
                return std::unexpected(make_error_code(error::unsupported_service));
        }
    }

    std::expected<void, std::error_code> encode_response_datagram(const span_uint8_t packet,
                                                                  const datagram& request,
                                                                  const std::uint8_t status) noexcept
    {
        if (const auto* tunnel = std::get_if<tunnelling_request_frame>(&request.payload))
        {
            if (request.service_type != frame::tunnelling_request_service)
                return std::unexpected(make_error_code(error::invalid_configuration));
            return frame::encode_tunnelling_ack_packet(packet,
                                                       tunnel->channel_id,
                                                       tunnel->sequence_number,
                                                       status);
        }

        if (const auto* heartbeat = std::get_if<connectionstate_request_frame>(&request.payload))
        {
            if (request.service_type != connection::connectionstate_request_service)
                return std::unexpected(make_error_code(error::invalid_configuration));
            return connection::encode_connectionstate_response_packet(
                packet, connectionstate_response_frame { heartbeat->channel_id,
                                                         static_cast<connect_status>(status) });
        }

        if (const auto* disconnect = std::get_if<disconnect_request_frame>(&request.payload))
        {
            if (request.service_type != connection::disconnect_request_service)
                return std::unexpected(make_error_code(error::invalid_configuration));
            return connection::encode_disconnect_response_packet(
                packet, disconnect_response_frame { disconnect->channel_id,
                                                    static_cast<connect_status>(status) });
        }

        return std::unexpected(make_error_code(error::unsupported_service));
    }
}
