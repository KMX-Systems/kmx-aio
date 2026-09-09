/// @file aio/knx/connection.hpp
/// @brief KNXnet/IP HPAI and tunnelling connection frame helpers.
/// @details
/// The four exchanges that open, keep and close a KNXnet/IP channel - CONNECT, CONNECTIONSTATE, DISCONNECT
/// and their responses - together with the host protocol address information (HPAI) they are built from.
///
/// An HPAI is how a KNXnet/IP peer says where to reach it, and a connection names two of them: a control
/// endpoint that carries the management exchanges, and a data endpoint that carries the traffic. They are
/// separate because they need not be the same socket, and the all-zero "route back" HPAI is how a client
/// behind NAT declines to name either - see @ref kmx::aio::knx::route_back.
///
/// Two connection types are modelled: a tunnelling connection, which is given an individual address on the
/// bus, and a device management connection, which is not because nothing it carries reaches the bus. The
/// decoders here are the only place the two are told apart on the wire.
/// @reference KNX System Specifications, 03/08/02 "Core", connection management.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <array>
        #include <cstdint>
        #include <expected>
        #include <span>
        #include <system_error>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/address.hpp>
    #include <kmx/aio/knx/frame.hpp>

namespace kmx::aio::knx
{
    /// @brief The status a server reports in a connection response.
    /// @details Every value but @ref connect_status::no_error is a refusal, and each says which part of the
    ///          request the server could not honour, so a client can tell a retryable condition - the
    ///          server being full - from a request it must change before asking again.
    enum class connect_status : std::uint8_t
    {
        /// @brief The request was accepted.
        no_error = 0x00u,
        /// @brief The host protocol named in an HPAI is not one the server supports.
        host_protocol_type = 0x21u,
        /// @brief The KNXnet/IP protocol version is not supported.
        version_not_supported = 0x22u,
        /// @brief The sequence number is out of step; the channel is no longer usable.
        sequence_number = 0x23u,
        /// @brief The server does not offer the requested connection type.
        connection_type = 0x24u,
        /// @brief The connection type is offered, but not with the requested options.
        connection_option = 0x25u,
        /// @brief The server has no free channel; retryable once another client disconnects.
        no_more_connections = 0x26u,
    };

    /// @brief An IPv4 address and UDP port, in host order.
    struct ipv4_endpoint
    {
        /// @brief The address, most significant octet first.
        ipv4::storage_t address {};
        /// @brief The UDP port.
        std::uint16_t port {};
    };

    /// @brief An IPv6 address and UDP port, in host order.
    struct ipv6_endpoint
    {
        /// @brief The address, most significant octet first.
        ipv6::storage_t address {};
        /// @brief The UDP port.
        std::uint16_t port {};
    };

    /// @brief Host protocol address information: where a KNXnet/IP peer can be reached over IPv4.
    /// @note An all-zero endpoint is not an unset one - it is the route-back request; see
    ///       @ref kmx::aio::knx::route_back.
    struct hpai
    {
        /// @brief The address and port to answer at.
        ipv4_endpoint endpoint {};
        /// @brief The host protocol code; `0x01` for UDP over IPv4, `0x02` for TCP.
        std::uint8_t protocol = 0x01u;
    };

    /// @brief Host protocol address information over IPv6.
    struct ipv6_hpai
    {
        /// @brief The address and port to answer at.
        ipv6_endpoint endpoint {};
        /// @brief The host protocol code.
        std::uint8_t protocol = 0x01u;
    };

    /// @brief Indicates whether an HPAI asks to be answered wherever the datagram came from.
    /// @param value The endpoint to test.
    /// @return `true` for the all-zero address and port.
    /// @details The route-back HPAI is how a client behind NAT is reachable at all: it cannot know the
    ///          address a server will see, so it names none and the server answers the source of the
    ///          datagram. Rejecting it as "port zero" locks the library out of every routed network,
    ///          which is the common case rather than an exotic one.
    /// @reference KNX System Specifications, 03/08/02 "Core", NAT-aware HPAI.
    [[nodiscard]] constexpr bool route_back(const hpai& value) noexcept
    {
        if (value.endpoint.port != 0u)
            return false;
        for (const auto octet: value.endpoint.address)
        {
            if (octet != 0u)
                return false;
        }
        return true;
    }

    /// @brief Indicates whether an IPv6 HPAI asks to be answered wherever the datagram came from.
    [[nodiscard]] constexpr bool route_back(const ipv6_hpai& value) noexcept
    {
        if (value.endpoint.port != 0u)
            return false;
        for (const auto octet: value.endpoint.address)
        {
            if (octet != 0u)
                return false;
        }
        return true;
    }

    /// @brief A CONNECT_REQUEST for a tunnelling connection over IPv6.
    struct ipv6_connect_request_frame
    {
        /// @brief Where to send the connection management exchanges.
        ipv6_hpai control_endpoint {};
        /// @brief Where to send the tunnelled traffic.
        ipv6_hpai data_endpoint {};
    };

    /// @brief The answer to an IPv6 tunnelling CONNECT_REQUEST.
    struct ipv6_connect_response_frame
    {
        /// @brief The channel the server allocated; meaningful only when @ref status is no_error.
        std::uint8_t channel_id {};
        /// @brief Whether the request was accepted, and if not, why.
        connect_status status = connect_status::no_error;
        /// @brief The server's own data endpoint, which the client sends traffic to.
        ipv6_hpai data_endpoint {};
        /// @brief The individual address the server assigned to this tunnel.
        individual_address assigned_address {};
    };

    /// @brief A CONNECT_REQUEST for a tunnelling connection.
    struct connect_request_frame
    {
        /// @brief Where to send the connection management exchanges.
        hpai control_endpoint {};
        /// @brief Where to send the tunnelled traffic.
        hpai data_endpoint {};
        /// @brief The KNX layer to tunnel at; link layer unless a monitor or raw connection is wanted.
        std::uint8_t knx_layer = 0x02u;
    };

    /// @brief A CONNECT_REQUEST for a device management connection.
    /// @details The other connection type a KNXnet/IP server offers. It carries no KNX layer and is answered
    ///          with no individual address, because nothing is tunnelled onto the bus: the channel carries
    ///          DEVICE_CONFIGURATION_REQUESTs that read and write the server's own interface objects.
    struct management_connect_request_frame
    {
        /// @brief Where to send the connection management exchanges.
        hpai control_endpoint {};
        /// @brief Where to send the device configuration traffic.
        hpai data_endpoint {};
    };

    /// @brief The answer to a device management CONNECT_REQUEST.
    struct management_connect_response_frame
    {
        /// @brief The channel the server allocated; meaningful only when @ref status is no_error.
        std::uint8_t channel_id {};
        /// @brief Whether the request was accepted, and if not, why.
        connect_status status = connect_status::no_error;
        /// @brief The server's own data endpoint, which the client sends traffic to.
        hpai data_endpoint {};
    };

    /// @brief The answer to a tunnelling CONNECT_REQUEST.
    struct connect_response_frame
    {
        /// @brief The channel the server allocated; meaningful only when @ref status is no_error.
        std::uint8_t channel_id {};
        /// @brief Whether the request was accepted, and if not, why.
        connect_status status = connect_status::no_error;
        /// @brief The server's own data endpoint, which the client sends traffic to.
        hpai data_endpoint {};
        /// @brief The individual address the interface assigned to this tunnel, from the CRD.
        /// @note The interface substitutes this address into every frame the client sends with an unset
        ///       source, which is why a tunnelling client has no address of its own to configure.
        individual_address assigned_address {};
    };

    /// @brief A CONNECTIONSTATE_REQUEST: the heartbeat that keeps a channel from being reaped.
    /// @note A server drops a channel it has heard nothing on; this is what a client sends to say it is
    ///       still there when it has no traffic of its own to send.
    struct connectionstate_request_frame
    {
        /// @brief The channel being asked about.
        std::uint8_t channel_id {};
    };

    /// @brief The answer to a CONNECTIONSTATE_REQUEST.
    struct connectionstate_response_frame
    {
        /// @brief The channel the answer is about.
        std::uint8_t channel_id {};
        /// @brief Whether the channel is still open, and if not, why.
        connect_status status = connect_status::no_error;
    };

    /// @brief A DISCONNECT_REQUEST, closing a channel.
    /// @note Either peer may send it; a server does so to reclaim a channel that has gone quiet.
    struct disconnect_request_frame
    {
        /// @brief The channel being closed.
        std::uint8_t channel_id {};
    };

    /// @brief The answer to a DISCONNECT_REQUEST.
    struct disconnect_response_frame
    {
        /// @brief The channel that was closed.
        std::uint8_t channel_id {};
        /// @brief The outcome of the close.
        connect_status status = connect_status::no_error;
    };

    namespace connection
    {
        /// @brief Service type of a CONNECT_REQUEST.
        inline constexpr std::uint16_t connect_request_service = 0x0205u;
        /// @brief Service type of a CONNECT_RESPONSE.
        inline constexpr std::uint16_t connect_response_service = 0x0206u;
        /// @brief Service type of a CONNECTIONSTATE_REQUEST.
        inline constexpr std::uint16_t connectionstate_request_service = 0x0207u;
        /// @brief Service type of a CONNECTIONSTATE_RESPONSE.
        inline constexpr std::uint16_t connectionstate_response_service = 0x0208u;
        /// @brief Service type of a DISCONNECT_REQUEST.
        inline constexpr std::uint16_t disconnect_request_service = 0x0209u;
        /// @brief Service type of a DISCONNECT_RESPONSE.
        inline constexpr std::uint16_t disconnect_response_service = 0x020Au;
        /// @brief Encoded size of an IPv4 HPAI: length, protocol, four address octets and a port.
        inline constexpr std::size_t hpai_size = 8u;
        /// @brief Encoded size of an IPv6 HPAI: length, protocol, sixteen address octets and a port.
        inline constexpr std::size_t ipv6_hpai_size = 20u;
        /// @brief Body size of a tunnelling CONNECT_REQUEST: two HPAIs and a four-octet CRI.
        inline constexpr std::size_t connect_request_body_size = 20u;
        /// @brief Body size of a tunnelling CONNECT_RESPONSE: channel, status, HPAI and a four-octet CRD.
        inline constexpr std::size_t connect_response_body_size = 14u;
        /// @brief Body size of an IPv6 tunnelling CONNECT_REQUEST.
        inline constexpr std::size_t ipv6_connect_request_body_size = 44u;
        /// @brief Body size of an IPv6 tunnelling CONNECT_RESPONSE.
        inline constexpr std::size_t ipv6_connect_response_body_size = 26u;
        /// @brief Structure length of the tunnelling connection request and response information blocks.
        inline constexpr std::uint8_t connection_information_size = 0x04u;
        /// @brief Structure length of a device management connection information block.
        /// @details A management connection names no layer and no address, so its block is the structure
        ///          length and the connection type and nothing else.
        inline constexpr std::uint8_t management_information_size = 0x02u;
        /// @brief Body size of a device management CONNECT_REQUEST: two HPAIs and a two-octet CRI.
        inline constexpr std::size_t management_connect_request_body_size = (2u * hpai_size) + management_information_size;
        /// @brief Body size of a device management CONNECT_RESPONSE: channel, status, HPAI and a CRD.
        inline constexpr std::size_t management_connect_response_body_size = 2u + hpai_size + management_information_size;
        /// @brief Connection type code of a tunnelling connection.
        inline constexpr std::uint8_t tunnel_connection_type = 0x04u;
        /// @brief Connection type code of a device management connection.
        inline constexpr std::uint8_t management_connection_type = 0x03u;
        /// @brief KNX layer code of a link layer tunnel, the layer ordinary group traffic uses.
        inline constexpr std::uint8_t tunnel_link_layer = 0x02u;
        /// @brief KNX layer code of a raw tunnel.
        inline constexpr std::uint8_t tunnel_raw_layer = 0x04u;
        /// @brief KNX layer code of a bus monitor tunnel.
        inline constexpr std::uint8_t tunnel_busmonitor_layer = 0x80u;

        /// @brief Indicates whether a code names a KNX layer this build can request.
        [[nodiscard]] constexpr bool valid_tunnel_layer(const std::uint8_t value) noexcept
        {
            return (value == tunnel_link_layer) || (value == tunnel_raw_layer) || (value == tunnel_busmonitor_layer);
        }

        /// @brief Encodes a tunnelling CONNECT_REQUEST.
        /// @param dest The destination octets; must be large enough for the encoded frame.
        /// @param request The request to encode; its KNX layer must be one @ref valid_tunnel_layer accepts.
        /// @return Nothing, or the reason the frame could not be encoded.
        [[nodiscard]] expected_void_t encode_connect_request_packet(span_uint8_t dest, const connect_request_frame& request) noexcept;
        /// @brief Encodes a device management CONNECT_REQUEST.
        /// @param dest The destination octets; must be large enough for the encoded frame.
        /// @param request The request to encode.
        /// @return Nothing, or the reason the frame could not be encoded.
        [[nodiscard]] expected_void_t encode_management_connect_request_packet(
            span_uint8_t dest, const management_connect_request_frame& request) noexcept;
        /// @brief Decodes a device management CONNECT_REQUEST.
        /// @param packet The received octets, header included.
        /// @return The decoded frame, or the reason the octets could not be read.
        /// @note Fails on a request whose connection information block names a tunnelling connection;
        ///       the two share a service type and are told apart only by that block.
        [[nodiscard]] std::expected<management_connect_request_frame, std::error_code> decode_management_connect_request_packet(
            cspan_uint8_t packet) noexcept;
        /// @brief Encodes a device management CONNECT_RESPONSE.
        /// @param dest The destination octets; must be large enough for the encoded frame.
        /// @param response The response to encode.
        /// @return Nothing, or the reason the frame could not be encoded.
        [[nodiscard]] expected_void_t encode_management_connect_response_packet(
            span_uint8_t dest, const management_connect_response_frame& response) noexcept;
        /// @brief Decodes a device management CONNECT_RESPONSE.
        /// @param packet The received octets, header included.
        /// @return The decoded frame, or the reason the octets could not be read.
        [[nodiscard]] std::expected<management_connect_response_frame, std::error_code> decode_management_connect_response_packet(
            cspan_uint8_t packet) noexcept;
        /// @brief Decodes a tunnelling CONNECT_REQUEST.
        /// @param packet The received octets, header included.
        /// @return The decoded frame, or the reason the octets could not be read.
        /// @note Fails on a request that names a device management connection, which
        ///       @ref decode_management_connect_request_packet reads instead.
        [[nodiscard]] std::expected<connect_request_frame, std::error_code> decode_connect_request_packet(
            cspan_uint8_t packet) noexcept;
        /// @brief Encodes a tunnelling CONNECT_REQUEST over IPv6.
        /// @param dest The destination octets; must be large enough for the encoded frame.
        /// @param request The request to encode.
        /// @return Nothing, or the reason the frame could not be encoded.
        [[nodiscard]] expected_void_t encode_ipv6_connect_request_packet(
            span_uint8_t dest, const ipv6_connect_request_frame& request) noexcept;
        /// @brief Decodes a tunnelling CONNECT_REQUEST over IPv6.
        /// @param packet The received octets, header included.
        /// @return The decoded frame, or the reason the octets could not be read.
        [[nodiscard]] std::expected<ipv6_connect_request_frame, std::error_code> decode_ipv6_connect_request_packet(
            cspan_uint8_t packet) noexcept;
        /// @brief Encodes a tunnelling CONNECT_RESPONSE.
        /// @param dest The destination octets; must be large enough for the encoded frame.
        /// @param response The response to encode; its status must be one @ref connect_status names.
        /// @return Nothing, or the reason the frame could not be encoded.
        /// @note The connection response data block is fixed-size, so the endpoint and assigned address are
        ///       written whatever the status says. A refusal reports them as whatever @p response holds.
        [[nodiscard]] expected_void_t encode_connect_response_packet(span_uint8_t dest, const connect_response_frame& response) noexcept;
        /// @brief Decodes a tunnelling CONNECT_RESPONSE.
        /// @param packet The received octets, header included.
        /// @return The decoded frame, or the reason the octets could not be read.
        /// @note The assigned address is read as data rather than checked against a fixed value, because
        ///       every interface hands out an address from its own line.
        [[nodiscard]] std::expected<connect_response_frame, std::error_code> decode_connect_response_packet(
            cspan_uint8_t packet) noexcept;
        /// @brief Encodes a tunnelling CONNECT_RESPONSE over IPv6.
        /// @param dest The destination octets; must be large enough for the encoded frame.
        /// @param response The response to encode.
        /// @return Nothing, or the reason the frame could not be encoded.
        [[nodiscard]] expected_void_t encode_ipv6_connect_response_packet(
            span_uint8_t dest, const ipv6_connect_response_frame& response) noexcept;
        /// @brief Decodes a tunnelling CONNECT_RESPONSE over IPv6.
        /// @param packet The received octets, header included.
        /// @return The decoded frame, or the reason the octets could not be read.
        [[nodiscard]] std::expected<ipv6_connect_response_frame, std::error_code> decode_ipv6_connect_response_packet(
            cspan_uint8_t packet) noexcept;
        /// @brief Encodes a CONNECTIONSTATE_REQUEST.
        /// @param dest The destination octets; must be large enough for the encoded frame.
        /// @param request The request to encode.
        /// @return Nothing, or the reason the frame could not be encoded.
        [[nodiscard]] expected_void_t encode_connectionstate_request_packet(
            span_uint8_t dest, const connectionstate_request_frame& request) noexcept;
        /// @brief Decodes a CONNECTIONSTATE_REQUEST.
        /// @param packet The received octets, header included.
        /// @return The decoded frame, or the reason the octets could not be read.
        [[nodiscard]] std::expected<connectionstate_request_frame, std::error_code> decode_connectionstate_request_packet(
            cspan_uint8_t packet) noexcept;
        /// @brief Encodes a CONNECTIONSTATE_RESPONSE.
        /// @param dest The destination octets; must be large enough for the encoded frame.
        /// @param response The response to encode.
        /// @return Nothing, or the reason the frame could not be encoded.
        [[nodiscard]] expected_void_t encode_connectionstate_response_packet(
            span_uint8_t dest, const connectionstate_response_frame& response) noexcept;
        /// @brief Decodes a CONNECTIONSTATE_RESPONSE.
        /// @param packet The received octets, header included.
        /// @return The decoded frame, or the reason the octets could not be read.
        [[nodiscard]] std::expected<connectionstate_response_frame, std::error_code> decode_connectionstate_response_packet(
            cspan_uint8_t packet) noexcept;
        /// @brief Encodes a DISCONNECT_REQUEST.
        /// @param dest The destination octets; must be large enough for the encoded frame.
        /// @param request The request to encode.
        /// @return Nothing, or the reason the frame could not be encoded.
        [[nodiscard]] expected_void_t encode_disconnect_request_packet(
            span_uint8_t dest, const disconnect_request_frame& request) noexcept;
        /// @brief Decodes a DISCONNECT_REQUEST.
        /// @param packet The received octets, header included.
        /// @return The decoded frame, or the reason the octets could not be read.
        [[nodiscard]] std::expected<disconnect_request_frame, std::error_code> decode_disconnect_request_packet(
            cspan_uint8_t packet) noexcept;
        /// @brief Encodes a DISCONNECT_RESPONSE.
        /// @param dest The destination octets; must be large enough for the encoded frame.
        /// @param response The response to encode.
        /// @return Nothing, or the reason the frame could not be encoded.
        [[nodiscard]] expected_void_t encode_disconnect_response_packet(
            span_uint8_t dest, const disconnect_response_frame& response) noexcept;
        /// @brief Decodes a DISCONNECT_RESPONSE.
        /// @param packet The received octets, header included.
        /// @return The decoded frame, or the reason the octets could not be read.
        [[nodiscard]] std::expected<disconnect_response_frame, std::error_code> decode_disconnect_response_packet(
            cspan_uint8_t packet) noexcept;
    }
}
#endif // KMX_AIO_FEATURE_KNX
