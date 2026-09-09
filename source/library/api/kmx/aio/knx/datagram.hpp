/// @file aio/knx/datagram.hpp
/// @brief Typed dispatch for supported KNXnet/IP datagrams.
/// @details
/// The service-specific headers each decode one KNXnet/IP service and know their own body layout. This
/// header is the layer above them: it reads the service type out of the common header once and hands the
/// body to the right decoder, so a receiver written against @ref kmx::aio::knx::datagram never has to
/// guess what arrived before it can parse it.
///
/// The variant is closed on purpose. A service this build does not model is reported as
/// @ref kmx::aio::knx::error::unsupported_service rather than being carried through as opaque octets,
/// because everything downstream acts on the decoded frame and has nothing to do with one it cannot read.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <expected>
        #include <system_error>
        #include <variant>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/task.hpp>
    #include <kmx/aio/knx/connection.hpp>
    #include <kmx/aio/knx/discovery.hpp>
    #include <kmx/aio/knx/frame.hpp>
    #include <kmx/aio/knx/routing.hpp>
    #include <kmx/aio/knx/secure.hpp>

namespace kmx::aio::knx
{
    /// @brief Every KNXnet/IP frame this build decodes, in service-type order.
    /// @details The alternative that comes back names the service, so a receiver switches on the decoded
    ///          type rather than on @ref datagram::service_type.
    using datagram_payload = std::variant<
        discovery::search_request_frame,
        discovery::ipv6_search_request_frame,
        discovery::search_response_frame,
        discovery::ipv6_search_response_frame,
        discovery::description_request_frame,
        discovery::description_response_frame,
        discovery::extended_search_request_frame,
        discovery::extended_search_response_frame,
        connect_request_frame,
        ipv6_connect_request_frame,
        management_connect_request_frame,
        connect_response_frame,
        ipv6_connect_response_frame,
        management_connect_response_frame,
        connectionstate_request_frame,
        connectionstate_response_frame,
        disconnect_request_frame,
        disconnect_response_frame,
        routing::indication,
        routing::lost_message,
        routing::busy,
        secure::packet,
        tunnelling_request_frame,
        tunnelling_ack_frame,
        device_configuration_frame,
        tunnelling_feature_frame>;

    /// @brief One decoded KNXnet/IP datagram: the service it announced, and the frame it carried.
    /// @details Both are kept because they are not quite redundant. Several services decode to the same
    ///          frame type - a TUNNELLING_REQUEST and a DEVICE_CONFIGURATION_REQUEST share a shape - so an
    ///          answer has to be built from the service that actually arrived, which is why
    ///          @ref encode_response_datagram checks the two agree before it replies.
    struct datagram
    {
        /// @brief The service type octet pair from the KNXnet/IP header.
        std::uint16_t service_type {};
        /// @brief The decoded body.
        datagram_payload payload;
    };

    /// @brief A decoded datagram, or the error explaining why the octets could not be decoded.
    using datagram_result_t = std::expected<datagram, std::error_code>;
    /// @brief Task yielding a decoded datagram or the error that stopped the receive.
    using datagram_task_t = task<datagram_result_t>;

    /// @brief Decodes one complete KNXnet/IP datagram.
    /// @param packet The received octets, header included.
    /// @return The decoded datagram, or the reason the octets could not be read.
    /// @retval kmx::aio::knx::error::unsupported_service The header was well formed but names a service
    ///         this build does not model.
    [[nodiscard]] datagram_result_t decode_datagram(cspan_uint8_t packet) noexcept;

    /// @brief Encodes one KNXnet/IP datagram, header included.
    /// @param packet The destination octets; must be large enough for the encoded frame.
    /// @param value The datagram to encode.
    /// @return Nothing, or the reason the datagram could not be encoded.
    [[nodiscard]] expected_void_t encode_datagram(span_uint8_t packet, const datagram& value) noexcept;

    /// @brief Encodes the answer a request expects, taking the channel and sequence from the request.
    /// @param packet The destination octets.
    /// @param request The decoded request being answered.
    /// @param status The status code to report; zero for success.
    /// @return Nothing, or the reason no answer could be built.
    /// @retval kmx::aio::knx::error::unsupported_service The request is one no answer is defined for -
    ///         a routing indication, for instance, which is never acknowledged.
    /// @retval kmx::aio::knx::error::invalid_configuration @p request carries a service type its decoded
    ///         frame does not belong to.
    /// @details The point of this over encoding the response by hand is that the correlating fields come
    ///          from the request itself. A TUNNELLING_ACK that echoes the wrong sequence number is not an
    ///          acknowledgement of anything, and it is exactly the field a caller mirrors incorrectly.
    [[nodiscard]] expected_void_t encode_response_datagram(
        span_uint8_t packet, const datagram& request, std::uint8_t status = 0u) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
