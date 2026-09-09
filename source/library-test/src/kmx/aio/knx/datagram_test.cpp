/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/datagram.hpp>
#include <kmx/aio/test/knx/telegram.hpp>

#include <array>
#include <cstdint>

namespace kmx::aio::test::knx::datagram_test
{
    using namespace kmx::aio::knx;

    TEST_CASE("knx datagram dispatcher returns typed tunnelling ack", "[knx][datagram][integration]")
    {
        std::array<std::uint8_t, 10u> packet {};
        REQUIRE(frame::encode_tunnelling_ack_packet(packet, 3u, 7u).has_value());

        const auto decoded = decode_datagram(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->service_type == frame::tunnelling_ack_service);
        const auto* ack = std::get_if<tunnelling_ack_frame>(&decoded->payload);
        REQUIRE(ack != nullptr);
        CHECK(ack->channel_id == 3u);
        CHECK(ack->sequence_number == 7u);
    }

    TEST_CASE("knx datagram dispatcher returns typed connection request", "[knx][datagram][integration]")
    {
        const connect_request_frame request {
            .control_endpoint = hpai { ipv4_endpoint { { 127u, 0u, 0u, 1u }, 3671u }, 0x01u },
            .data_endpoint = hpai { ipv4_endpoint { { 127u, 0u, 0u, 1u }, 3672u }, 0x01u },
        };
        std::array<std::uint8_t, 26u> packet {};
        REQUIRE(connection::encode_connect_request_packet(packet, request).has_value());

        const auto decoded = decode_datagram(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->service_type == connection::connect_request_service);
        const auto* connect = std::get_if<connect_request_frame>(&decoded->payload);
        REQUIRE(connect != nullptr);
        CHECK(connect->data_endpoint.endpoint.port == 3672u);
    }

    TEST_CASE("knx datagram dispatcher rejects unknown services after header validation", "[knx][datagram][unit]")
    {
        const std::array<std::uint8_t, 6u> packet { 0x06u, 0x10u, 0x7Fu, 0xFFu, 0x00u, 0x06u };
        const auto decoded = decode_datagram(packet);
        REQUIRE(!decoded.has_value());
        CHECK(decoded.error() == make_error_code(error::unsupported_service));
    }

    TEST_CASE("knx datagram encoder rejects a service and payload mismatch", "[knx][datagram][unit]")
    {
        const datagram value {
            .service_type = frame::tunnelling_ack_service,
            .payload = tunnelling_request_frame {},
        };
        std::array<std::uint8_t, 10u> packet {};

        const auto result = encode_datagram(packet, value);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::invalid_configuration));
    }

    TEST_CASE("knx datagram encoder round-trips a disconnect response", "[knx][datagram][integration]")
    {
        const datagram value {
            .service_type = connection::disconnect_response_service,
            .payload = disconnect_response_frame { 4u, connect_status::no_error },
        };
        std::array<std::uint8_t, 8u> packet {};
        REQUIRE(encode_datagram(packet, value).has_value());

        const auto decoded = decode_datagram(packet);
        REQUIRE(decoded.has_value());
        const auto* response = std::get_if<disconnect_response_frame>(&decoded->payload);
        REQUIRE(response != nullptr);
        CHECK(response->channel_id == 4u);
    }

    TEST_CASE("knx datagram preserves and re-encodes a tunnelling request", "[knx][datagram][integration]")
    {
        const auto& cemi = sample_cemi;
        std::array<std::uint8_t, sample_tunnelling_packet_size> original {};
        REQUIRE(frame::encode_tunnelling_request_packet(original, 2u, 8u, cemi).has_value());

        const auto decoded = decode_datagram(original);
        REQUIRE(decoded.has_value());
        const auto* request = std::get_if<tunnelling_request_frame>(&decoded->payload);
        REQUIRE(request != nullptr);
        CHECK(request->cemi_bytes == std::vector<std::uint8_t>(cemi.begin(), cemi.end()));

        std::array<std::uint8_t, sample_tunnelling_packet_size> encoded {};
        REQUIRE(encode_datagram(encoded, decoded.value()).has_value());
        CHECK(encoded == original);
    }

    TEST_CASE("knx datagram rejects a tunnelling request without cEMI bytes", "[knx][datagram][unit]")
    {
        const datagram value {
            .service_type = frame::tunnelling_request_service,
            .payload = tunnelling_request_frame { .channel_id = 2u, .sequence_number = 8u },
        };
        std::array<std::uint8_t, sample_tunnelling_packet_size> packet {};

        const auto result = encode_datagram(packet, value);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx datagram encodes a tunnelling response", "[knx][datagram][integration]")
    {
        const auto& cemi = sample_cemi;
        std::array<std::uint8_t, sample_tunnelling_packet_size> request_packet {};
        REQUIRE(frame::encode_tunnelling_request_packet(request_packet, 2u, 8u, cemi).has_value());
        const auto request = decode_datagram(request_packet);
        REQUIRE(request.has_value());

        std::array<std::uint8_t, 10u> response_packet {};
        REQUIRE(encode_response_datagram(response_packet, request.value()).has_value());
        const auto response = decode_datagram(response_packet);
        REQUIRE(response.has_value());
        const auto* ack = std::get_if<tunnelling_ack_frame>(&response->payload);
        REQUIRE(ack != nullptr);
        CHECK(ack->channel_id == 2u);
        CHECK(ack->sequence_number == 8u);
        CHECK(ack->status == 0u);
    }

    TEST_CASE("knx datagram encodes heartbeat and disconnect responses", "[knx][datagram][integration]")
    {
        const datagram heartbeat {
            .service_type = connection::connectionstate_request_service,
            .payload = connectionstate_request_frame { 4u },
        };
        std::array<std::uint8_t, 8u> heartbeat_packet {};
        REQUIRE(encode_response_datagram(heartbeat_packet, heartbeat).has_value());
        const auto heartbeat_response = decode_datagram(heartbeat_packet);
        REQUIRE(heartbeat_response.has_value());
        CHECK(std::get<connectionstate_response_frame>(heartbeat_response->payload).channel_id == 4u);

        const datagram disconnect {
            .service_type = connection::disconnect_request_service,
            .payload = disconnect_request_frame { 4u },
        };
        std::array<std::uint8_t, 8u> disconnect_packet {};
        REQUIRE(encode_response_datagram(disconnect_packet, disconnect, 0x24u).has_value());
        const auto disconnect_response = decode_datagram(disconnect_packet);
        REQUIRE(disconnect_response.has_value());
        CHECK(std::get<disconnect_response_frame>(disconnect_response->payload).status ==
              connect_status::connection_type);
    }

    TEST_CASE("knx datagram dispatches IPv6 CONNECT frames", "[knx][datagram][integration]")
    {
        const ipv6_connect_request_frame request {
            .control_endpoint =
                ipv6_hpai {ipv6_endpoint {{0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u}, 3671u}, 0x01u},
            .data_endpoint =
                ipv6_hpai {ipv6_endpoint {{15u, 14u, 13u, 12u, 11u, 10u, 9u, 8u, 7u, 6u, 5u, 4u, 3u, 2u, 1u, 0u}, 3672u}, 0x01u},
        };
        std::array<std::uint8_t, 50u> packet {};
        REQUIRE(connection::encode_ipv6_connect_request_packet(packet, request).has_value());
        const auto decoded = decode_datagram(packet);
        REQUIRE(decoded.has_value());
        CHECK(std::holds_alternative<ipv6_connect_request_frame>(decoded->payload));
        CHECK(std::get<ipv6_connect_request_frame>(decoded->payload).data_endpoint.endpoint.port == 3672u);
    }

    TEST_CASE("knx datagram dispatches IPv6 SEARCH frames", "[knx][datagram][integration]")
    {
        const discovery::ipv6_search_request_frame request {
            .discovery_endpoint = ipv6_hpai {
                ipv6_endpoint {{0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u}, 3671u},
                0x01u,
            },
        };
        std::array<std::uint8_t, 26u> packet {};
        REQUIRE(discovery::encode_ipv6_search_request_packet(packet, request).has_value());
        const auto decoded = decode_datagram(packet);
        REQUIRE(decoded.has_value());
        CHECK(std::holds_alternative<discovery::ipv6_search_request_frame>(decoded->payload));
        std::array<std::uint8_t, 26u> reencoded {};
        REQUIRE(encode_datagram(reencoded, decoded.value()).has_value());
        CHECK(reencoded == packet);
    }

    TEST_CASE("knx datagram dispatches routing control services", "[knx][datagram][routing][integration]")
    {
        // ROUTING_BUSY body: structure length 0x06, device state, wait time, control field.
        const std::array<std::uint8_t, 12u> original_busy {
            0x06u, 0x10u, 0x05u, 0x32u, 0x00u, 0x0Cu, 0x06u, 0x01u, 0x01u, 0x2Cu, 0x00u, 0x02u,
        };
        const auto busy = decode_datagram(original_busy);
        REQUIRE(busy.has_value());
        REQUIRE(std::holds_alternative<routing::busy>(busy->payload));
        CHECK(std::get<routing::busy>(busy->payload).device_state == 0x01u);
        CHECK(std::get<routing::busy>(busy->payload).wait_time_ms == 300u);
        CHECK(std::get<routing::busy>(busy->payload).control_field == 0x0002u);
        std::array<std::uint8_t, 12u> encoded_busy {};
        REQUIRE(encode_datagram(encoded_busy, busy.value()).has_value());
        CHECK(encoded_busy == original_busy);

        const datagram lost {
            .service_type = routing::lost_message_service,
            .payload = routing::lost_message {.device_state = 0x03u, .count = 4u},
        };
        std::array<std::uint8_t, 10u> encoded_lost {};
        REQUIRE(encode_datagram(encoded_lost, lost).has_value());
        const auto decoded_lost = decode_datagram(encoded_lost);
        REQUIRE(decoded_lost.has_value());
        CHECK(std::get<routing::lost_message>(decoded_lost->payload).device_state == 0x03u);
        CHECK(std::get<routing::lost_message>(decoded_lost->payload).count == 4u);
    }

    TEST_CASE("knx datagram rejects responses for non-request payloads", "[knx][datagram][unit]")
    {
        const datagram response {
            .service_type = frame::tunnelling_ack_service,
            .payload = tunnelling_ack_frame { 2u, 8u, 0u },
        };
        std::array<std::uint8_t, 10u> packet {};
        const auto result = encode_response_datagram(packet, response);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::unsupported_service));
    }
}
