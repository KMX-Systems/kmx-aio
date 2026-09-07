/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/connection.hpp>

#include <array>
#include <cstdint>

namespace kmx::aio::test::knx::connection_test
{
    using namespace kmx::aio::knx;

    constexpr ipv4_endpoint control_address {{ 127u, 0u, 0u, 1u }, 3671u };
    constexpr ipv4_endpoint data_address {{ 127u, 0u, 0u, 1u }, 3672u };
    constexpr ipv6_endpoint ipv6_control_address {{0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u}, 3671u};
    constexpr ipv6_endpoint ipv6_data_address {{15u, 14u, 13u, 12u, 11u, 10u, 9u, 8u, 7u, 6u, 5u, 4u, 3u, 2u, 1u, 0u}, 3672u};

    TEST_CASE("knx connect request round-trips IPv4 HPAI", "[knx][connection][integration]")
    {
        const connect_request_frame request {
            .control_endpoint = hpai { control_address, 0x01u },
            .data_endpoint = hpai { data_address, 0x01u },
        };
        std::array<std::uint8_t, 26u> packet {};

        REQUIRE(connection::encode_connect_request_packet(packet, request).has_value());
        const auto decoded = connection::decode_connect_request_packet(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->control_endpoint.endpoint.address == control_address.address);
        CHECK(decoded->control_endpoint.endpoint.port == 3671u);
        CHECK(decoded->data_endpoint.endpoint.port == 3672u);
    }

    TEST_CASE("knx connect response round-trips status and channel", "[knx][connection][integration]")
    {
        const connect_response_frame response {
            .channel_id = 9u,
            .status = connect_status::no_error,
            .data_endpoint = hpai { data_address, 0x01u },
        };
        std::array<std::uint8_t, 20u> packet {};

        REQUIRE(connection::encode_connect_response_packet(packet, response).has_value());
        const auto decoded = connection::decode_connect_response_packet(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->channel_id == 9u);
        CHECK(decoded->status == connect_status::no_error);
        CHECK(decoded->data_endpoint.endpoint.port == 3672u);
    }

    TEST_CASE("knx IPv6 connect request round-trips HPAI", "[knx][connection][integration]")
    {
        const ipv6_connect_request_frame request {
            .control_endpoint = ipv6_hpai {ipv6_control_address, 0x01u},
            .data_endpoint = ipv6_hpai {ipv6_data_address, 0x01u},
        };
        std::array<std::uint8_t, 50u> packet {};
        REQUIRE(connection::encode_ipv6_connect_request_packet(packet, request).has_value());
        CHECK(packet[0u] == 0x06u);
        CHECK(packet[4u] == 0x00u);
        CHECK(packet[5u] == 50u);
        CHECK(packet[6u] == 20u);
        const auto decoded = connection::decode_ipv6_connect_request_packet(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->control_endpoint.endpoint.address == ipv6_control_address.address);
        CHECK(decoded->control_endpoint.endpoint.port == 3671u);
        CHECK(decoded->data_endpoint.endpoint.address == ipv6_data_address.address);
        CHECK(decoded->data_endpoint.endpoint.port == 3672u);
    }

    TEST_CASE("knx IPv6 connect response round-trips assigned address", "[knx][connection][integration]")
    {
        const ipv6_connect_response_frame response {
            .channel_id = 9u,
            .status = connect_status::no_error,
            .data_endpoint = ipv6_hpai {ipv6_data_address, 0x01u},
            .assigned_address = individual_address {1u, 1u, 20u},
        };
        std::array<std::uint8_t, 32u> packet {};
        REQUIRE(connection::encode_ipv6_connect_response_packet(packet, response).has_value());
        const auto decoded = connection::decode_ipv6_connect_response_packet(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->channel_id == 9u);
        CHECK(decoded->data_endpoint.endpoint.address == ipv6_data_address.address);
        CHECK(decoded->data_endpoint.endpoint.port == 3672u);
        CHECK(decoded->assigned_address == response.assigned_address);
    }

    TEST_CASE("knx connect decoder rejects IPv6 HPAI and wrong layer", "[knx][connection][unit]")
    {
        const connect_response_frame response {
            .channel_id = 9u,
            .status = connect_status::no_error,
            .data_endpoint = hpai { data_address, 0x01u },
        };
        std::array<std::uint8_t, 20u> packet {};
        REQUIRE(connection::encode_connect_response_packet(packet, response).has_value());

        packet[9u] = 0x02u;
        const auto hpai_result = connection::decode_connect_response_packet(packet);
        REQUIRE(!hpai_result.has_value());
        CHECK(hpai_result.error() == make_error_code(error::unsupported_hpai));

        REQUIRE(connection::encode_connect_response_packet(packet, response).has_value());
        packet[17u] = 0x03u; // a connection type that is not a tunnel
        const auto layer_result = connection::decode_connect_response_packet(packet);
        REQUIRE(!layer_result.has_value());
        CHECK(layer_result.error() == make_error_code(error::unsupported_connection_type));

        REQUIRE(connection::encode_connect_response_packet(packet, response).has_value());
        packet[8u] = 20u;
        const auto ipv6_result = connection::decode_connect_response_packet(packet);
        REQUIRE(!ipv6_result.has_value());
        CHECK(ipv6_result.error() == make_error_code(error::unsupported_hpai));
    }

    TEST_CASE("knx connect request carries the tunnelling connection request information", "[knx][connection][unit]")
    {
        // The connection request information is structure length, connection type, KNX layer, reserved -
        // in that order. Getting the last two the wrong way round asks for layer 0x00, which no interface
        // accepts, and the mistake is invisible to a round-trip test that decodes what it just encoded.
        const connect_request_frame request {
            .control_endpoint = hpai {control_address, 0x01u},
            .data_endpoint = hpai {data_address, 0x01u},
        };
        std::array<std::uint8_t, 26u> packet {};
        REQUIRE(connection::encode_connect_request_packet(packet, request).has_value());

        constexpr std::array<std::uint8_t, 26u> expected {
            0x06u, 0x10u, 0x02u, 0x05u, 0x00u, 0x1Au,                                    // header
            0x08u, 0x01u, 127u,  0u,    0u,    1u,    0x0Eu, 0x57u,                      // control HPAI
            0x08u, 0x01u, 127u,  0u,    0u,    1u,    0x0Eu, 0x58u,                      // data HPAI
            0x04u, 0x04u, 0x02u, 0x00u,                                                  // CRI
        };
        CHECK(packet == expected);
    }

    TEST_CASE("knx connect response carries the address the interface assigned", "[knx][connection][unit]")
    {
        // The last two octets of the connection response data block are the individual address the
        // interface handed out. Treating them as a constant refuses every interface but one.
        const connect_response_frame response {
            .channel_id = 9u,
            .status = connect_status::no_error,
            .data_endpoint = hpai {data_address, 0x01u},
            .assigned_address = individual_address {1u, 1u, 10u},
        };
        std::array<std::uint8_t, 20u> packet {};
        REQUIRE(connection::encode_connect_response_packet(packet, response).has_value());

        CHECK(packet[16u] == 0x04u);
        CHECK(packet[17u] == 0x04u);
        CHECK(packet[18u] == 0x11u);
        CHECK(packet[19u] == 0x0Au);

        const auto decoded = connection::decode_connect_response_packet(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->assigned_address == individual_address {1u, 1u, 10u});
        CHECK(decoded->assigned_address.to_string() == "1.1.10");
    }

    TEST_CASE("knx connect response accepts any assigned address", "[knx][connection][unit]")
    {
        for (const auto& text: {"0.0.0", "1.1.1", "15.15.255", "2.3.4"})
        {
            const auto address = individual_address::parse(text);
            REQUIRE(address.has_value());

            const connect_response_frame response {
                .channel_id = 1u,
                .status = connect_status::no_error,
                .data_endpoint = hpai {data_address, 0x01u},
                .assigned_address = *address,
            };
            std::array<std::uint8_t, 20u> packet {};
            REQUIRE(connection::encode_connect_response_packet(packet, response).has_value());

            const auto decoded = connection::decode_connect_response_packet(packet);
            REQUIRE(decoded.has_value());
            CHECK(decoded->assigned_address == *address);
        }
    }

    TEST_CASE("knx connection-state frames round-trip", "[knx][connection][integration]")
    {
        std::array<std::uint8_t, 8u> request_packet {};
        REQUIRE(connection::encode_connectionstate_request_packet(request_packet,
                                                                  connectionstate_request_frame { 9u }).has_value());
        const auto request = connection::decode_connectionstate_request_packet(request_packet);
        REQUIRE(request.has_value());
        CHECK(request->channel_id == 9u);

        std::array<std::uint8_t, 8u> response_packet {};
        REQUIRE(connection::encode_connectionstate_response_packet(response_packet,
                                                                   connectionstate_response_frame { 9u, connect_status::no_error }).has_value());
        const auto response = connection::decode_connectionstate_response_packet(response_packet);
        REQUIRE(response.has_value());
        CHECK(response->channel_id == 9u);
        CHECK(response->status == connect_status::no_error);
    }

    TEST_CASE("knx disconnect frames round-trip", "[knx][connection][integration]")
    {
        std::array<std::uint8_t, 8u> request_packet {};
        REQUIRE(connection::encode_disconnect_request_packet(request_packet,
                                                            disconnect_request_frame { 9u }).has_value());
        const auto request = connection::decode_disconnect_request_packet(request_packet);
        REQUIRE(request.has_value());
        CHECK(request->channel_id == 9u);

        std::array<std::uint8_t, 8u> response_packet {};
        REQUIRE(connection::encode_disconnect_response_packet(response_packet,
                                                             disconnect_response_frame { 9u, connect_status::no_error }).has_value());
        const auto response = connection::decode_disconnect_response_packet(response_packet);
        REQUIRE(response.has_value());
        CHECK(response->channel_id == 9u);
        CHECK(response->status == connect_status::no_error);
    }

    TEST_CASE("knx connection decoders reject unknown status values", "[knx][connection][unit]")
    {
        const std::array<std::uint8_t, 8u> packet {
            0x06u, 0x10u, 0x02u, 0x08u, 0x00u, 0x08u, 0x09u, 0xFFu,
        };
        const auto heartbeat = connection::decode_connectionstate_response_packet(packet);
        REQUIRE(!heartbeat.has_value());
        CHECK(heartbeat.error() == make_error_code(error::malformed_frame));

        const std::array<std::uint8_t, 20u> connect_packet {
            0x06u, 0x10u, 0x02u, 0x06u, 0x00u, 0x14u, 0x01u, 0xFFu,
            0x08u, 0x01u, 127u, 0u, 0u, 1u, 0x0Eu, 0x58u,
            0x04u, 0x04u, 0x00u, 0x02u,
        };
        const auto connect = connection::decode_connect_response_packet(connect_packet);
        REQUIRE(!connect.has_value());
        CHECK(connect.error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx connection encoders reject unknown status values", "[knx][connection][unit]")
    {
        std::array<std::uint8_t, 8u> heartbeat {};
        const auto heartbeat_result = connection::encode_connectionstate_response_packet(
            heartbeat, connectionstate_response_frame { 1u, static_cast<connect_status>(0xFFu) });
        REQUIRE(!heartbeat_result.has_value());
        CHECK(heartbeat_result.error() == make_error_code(error::invalid_configuration));

        std::array<std::uint8_t, 8u> disconnect {};
        const auto disconnect_result = connection::encode_disconnect_response_packet(
            disconnect, disconnect_response_frame { 1u, static_cast<connect_status>(0xFFu) });
        REQUIRE(!disconnect_result.has_value());
        CHECK(disconnect_result.error() == make_error_code(error::invalid_configuration));
    }

    TEST_CASE("knx connection encoders reject unsupported HPAI protocols", "[knx][connection][unit]")
    {
        const connect_request_frame request {
            .control_endpoint = hpai { control_address, 0x02u },
            .data_endpoint = hpai { data_address, 0x01u },
        };
        std::array<std::uint8_t, 26u> packet {};
        const auto request_result = connection::encode_connect_request_packet(packet, request);
        REQUIRE(!request_result.has_value());
        CHECK(request_result.error() == make_error_code(error::unsupported_hpai));

        const connect_response_frame response {
            .channel_id = 3u,
            .status = connect_status::no_error,
            .data_endpoint = hpai { data_address, 0x02u },
        };
        std::array<std::uint8_t, 20u> response_packet {};
        const auto response_result = connection::encode_connect_response_packet(response_packet, response);
        REQUIRE(!response_result.has_value());
        CHECK(response_result.error() == make_error_code(error::unsupported_hpai));
    }
}
