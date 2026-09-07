/// @file aio/knx/datagram.hpp
/// @brief Typed dispatch for supported KNXnet/IP datagrams.
#pragma once
#ifndef PCH
    #include <expected>
    #include <system_error>
    #include <variant>
#endif

#include <kmx/aio/basic_types.hpp>
#include <kmx/aio/knx/connection.hpp>
#include <kmx/aio/knx/discovery.hpp>
#include <kmx/aio/knx/frame.hpp>
#include <kmx/aio/knx/routing.hpp>
#include <kmx/aio/knx/secure.hpp>

namespace kmx::aio::knx
{
    using datagram_payload = std::variant<
        discovery::search_request_frame,
        discovery::ipv6_search_request_frame,
        discovery::search_response_frame,
        discovery::ipv6_search_response_frame,
        discovery::description_request_frame,
        discovery::description_response_frame,
        connect_request_frame,
        ipv6_connect_request_frame,
        connect_response_frame,
        ipv6_connect_response_frame,
        connectionstate_request_frame,
        connectionstate_response_frame,
        disconnect_request_frame,
        disconnect_response_frame,
        routing::indication,
        routing::lost_message,
        routing::busy,
        secure::packet,
        tunnelling_request_frame,
        tunnelling_ack_frame>;

    struct datagram
    {
        std::uint16_t service_type = 0u;
        datagram_payload payload;
    };

    [[nodiscard]] std::expected<datagram, std::error_code> decode_datagram(cspan_uint8_t packet) noexcept;
    [[nodiscard]] std::expected<void, std::error_code> encode_datagram(
        span_uint8_t packet, const datagram& value) noexcept;
    [[nodiscard]] std::expected<void, std::error_code> encode_response_datagram(
        span_uint8_t packet, const datagram& request, std::uint8_t status = 0u) noexcept;
}
