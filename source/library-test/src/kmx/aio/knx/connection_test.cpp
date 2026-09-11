/// @file src/kmx/aio/knx/connection_test.cpp
/// @brief Unit tests for KNXnet/IP CONNECT, CONNECTIONSTATE and DISCONNECT frames with IPv4, IPv6 and TCP HPAIs.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/connection.hpp>
#ifndef PCH
    #include <kmx/aio/knx/datagram.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <array>
    #include <cstdint>
    #include <variant>
#endif

namespace kmx::aio::test::knx::connection_test
{
    using namespace kmx::aio::knx;

    constexpr ipv4_endpoint control_address {{127u, 0u, 0u, 1u}, 3671u};
    constexpr ipv4_endpoint data_address {{127u, 0u, 0u, 1u}, 3672u};
    constexpr ipv6_endpoint ipv6_control_address {{0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u}, 3671u};
    constexpr ipv6_endpoint ipv6_data_address {{15u, 14u, 13u, 12u, 11u, 10u, 9u, 8u, 7u, 6u, 5u, 4u, 3u, 2u, 1u, 0u}, 3672u};

    TEST_CASE("knx connect request round-trips IPv4 HPAI", "[knx][connection][integration]")
    {
        const connect_request_frame request {
            .control_endpoint = hpai {control_address, 0x01u},
            .data_endpoint = hpai {data_address, 0x01u},
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
            .data_endpoint = hpai {data_address, 0x01u},
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
            .data_endpoint = hpai {data_address, 0x01u},
        };
        std::array<std::uint8_t, 20u> packet {};
        REQUIRE(connection::encode_connect_response_packet(packet, response).has_value());

        // TCP (0x02) is a host protocol KNXnet/IP defines, so it decodes and the endpoint decides whether it
        // serves it; a code the protocol does not define is refused by the decoder itself.
        packet[9u] = 0x02u;
        const auto tcp_result = connection::decode_connect_response_packet(packet);
        REQUIRE(tcp_result.has_value());
        CHECK(tcp_result->data_endpoint.protocol == 0x02u);

        packet[9u] = 0x03u;
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
            0x06u, 0x10u, 0x02u, 0x05u, 0x00u, 0x1Au,               // header
            0x08u, 0x01u, 127u,  0u,    0u,    1u,    0x0Eu, 0x57u, // control HPAI
            0x08u, 0x01u, 127u,  0u,    0u,    1u,    0x0Eu, 0x58u, // data HPAI
            0x04u, 0x04u, 0x02u, 0x00u,                             // CRI
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
        // A CONNECTIONSTATE_REQUEST names the channel and the control endpoint its answer goes to: sixteen octets.
        const connectionstate_request_frame heartbeat {.channel_id = 9u,
                                                       .control_endpoint = hpai {ipv4_endpoint {{192u, 168u, 1u, 10u}, 3671u}, 0x01u}};
        std::array<std::uint8_t, 16u> request_packet {};
        REQUIRE(connection::encode_connectionstate_request_packet(request_packet, heartbeat).has_value());
        constexpr std::array<std::uint8_t, 16u> expected {0x06u, 0x10u, 0x02u, 0x07u, 0x00u, 0x10u, 0x09u, 0x00u,
                                                          0x08u, 0x01u, 0xC0u, 0xA8u, 0x01u, 0x0Au, 0x0Eu, 0x57u};
        CHECK(request_packet == expected);
        const auto request = connection::decode_connectionstate_request_packet(request_packet);
        REQUIRE(request.has_value());
        CHECK(request->channel_id == 9u);
        CHECK(request->control_endpoint.endpoint.port == 3671u);

        // The eight-octet form without the endpoint is not a request any peer sends, and is refused.
        constexpr std::array<std::uint8_t, 8u> truncated {0x06u, 0x10u, 0x02u, 0x07u, 0x00u, 0x08u, 0x09u, 0x00u};
        const auto refused = connection::decode_connectionstate_request_packet(truncated);
        REQUIRE(!refused.has_value());
        CHECK(refused.error() == make_error_code(error::malformed_frame));

        std::array<std::uint8_t, 8u> response_packet {};
        REQUIRE(connection::encode_connectionstate_response_packet(response_packet,
                                                                   connectionstate_response_frame {9u, connect_status::no_error})
                    .has_value());
        const auto response = connection::decode_connectionstate_response_packet(response_packet);
        REQUIRE(response.has_value());
        CHECK(response->channel_id == 9u);
        CHECK(response->status == connect_status::no_error);
    }

    TEST_CASE("knx disconnect frames round-trip", "[knx][connection][integration]")
    {
        // Over TCP the control endpoint is the TCP HPAI: protocol 0x02, no address, no port.
        const disconnect_request_frame request_frame {.channel_id = 9u, .control_endpoint = hpai {{}, 0x02u}};
        std::array<std::uint8_t, 16u> request_packet {};
        REQUIRE(connection::encode_disconnect_request_packet(request_packet, request_frame).has_value());
        constexpr std::array<std::uint8_t, 16u> expected {0x06u, 0x10u, 0x02u, 0x09u, 0x00u, 0x10u, 0x09u, 0x00u,
                                                          0x08u, 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
        CHECK(request_packet == expected);
        const auto request = connection::decode_disconnect_request_packet(request_packet);
        REQUIRE(request.has_value());
        CHECK(request->channel_id == 9u);
        CHECK(request->control_endpoint.protocol == 0x02u);
        std::array<std::uint8_t, 15u> short_packet {};
        const auto too_short = connection::encode_disconnect_request_packet(short_packet, request_frame);
        REQUIRE(!too_short.has_value());
        CHECK(too_short.error() == make_error_code(error::invalid_length));

        std::array<std::uint8_t, 8u> response_packet {};
        REQUIRE(connection::encode_disconnect_response_packet(response_packet, disconnect_response_frame {9u, connect_status::no_error})
                    .has_value());
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
            0x06u, 0x10u, 0x02u, 0x06u, 0x00u, 0x14u, 0x01u, 0xFFu, 0x08u, 0x01u,
            127u,  0u,    0u,    1u,    0x0Eu, 0x58u, 0x04u, 0x04u, 0x00u, 0x02u,
        };
        const auto connect = connection::decode_connect_response_packet(connect_packet);
        REQUIRE(!connect.has_value());
        CHECK(connect.error() == make_error_code(error::malformed_frame));
    }

    // The status octets are KNXnet/IP error codes, and they are pinned to the wire here rather than
    // round-tripped: an encoder and a decoder that agree on a wrong number pass every round trip, which is
    // how this enumeration once numbered "no more connections" 0x26 - E_DATA_CONNECTION to everyone else.
    TEST_CASE("knx connection status codes carry the KNXnet/IP values", "[knx][connection][unit]")
    {
        STATIC_CHECK(static_cast<std::uint8_t>(connect_status::no_error) == 0x00u);
        STATIC_CHECK(static_cast<std::uint8_t>(connect_status::host_protocol_type) == 0x01u);
        STATIC_CHECK(static_cast<std::uint8_t>(connect_status::version_not_supported) == 0x02u);
        STATIC_CHECK(static_cast<std::uint8_t>(connect_status::sequence_number) == 0x04u);
        STATIC_CHECK(static_cast<std::uint8_t>(connect_status::connection_id) == 0x21u);
        STATIC_CHECK(static_cast<std::uint8_t>(connect_status::connection_type) == 0x22u);
        STATIC_CHECK(static_cast<std::uint8_t>(connect_status::connection_option) == 0x23u);
        STATIC_CHECK(static_cast<std::uint8_t>(connect_status::no_more_connections) == 0x24u);
        STATIC_CHECK(static_cast<std::uint8_t>(connect_status::no_more_unique_connections) == 0x25u);
        STATIC_CHECK(static_cast<std::uint8_t>(connect_status::data_connection) == 0x26u);
        STATIC_CHECK(static_cast<std::uint8_t>(connect_status::knx_connection) == 0x27u);
        STATIC_CHECK(static_cast<std::uint8_t>(connect_status::authorisation_error) == 0x28u);
        STATIC_CHECK(static_cast<std::uint8_t>(connect_status::tunnelling_layer) == 0x29u);
        STATIC_CHECK(static_cast<std::uint8_t>(connect_status::no_tunnelling_address) == 0x2Du);
        STATIC_CHECK(static_cast<std::uint8_t>(connect_status::connection_in_use) == 0x2Eu);
    }

    TEST_CASE("knx connection decoders accept every KNXnet/IP status code", "[knx][connection][unit]")
    {
        constexpr std::array<std::uint8_t, 15u> codes {
            0x00u, 0x01u, 0x02u, 0x04u, 0x21u, 0x22u, 0x23u, 0x24u, 0x25u, 0x26u, 0x27u, 0x28u, 0x29u, 0x2Du, 0x2Eu,
        };
        for (const auto code: codes)
        {
            INFO("status=" << static_cast<unsigned>(code));
            const std::array<std::uint8_t, 8u> heartbeat {0x06u, 0x10u, 0x02u, 0x08u, 0x00u, 0x08u, 0x07u, code};
            const auto decoded = connection::decode_connectionstate_response_packet(heartbeat);
            REQUIRE(decoded.has_value());
            CHECK(static_cast<std::uint8_t>(decoded->status) == code);
        }
    }

    TEST_CASE("knx connection refusals decode from their wire octets", "[knx][connection][unit]")
    {
        // A full server answers with the eight-octet form: header, channel 0, E_NO_MORE_CONNECTIONS.
        constexpr std::array<std::uint8_t, 8u> full {0x06u, 0x10u, 0x02u, 0x06u, 0x00u, 0x08u, 0x00u, 0x24u};
        const auto refused = connection::decode_connect_response_packet(full);
        REQUIRE(refused.has_value());
        CHECK(refused->status == connect_status::no_more_connections);

        // A heartbeat for a channel the server has already reclaimed.
        constexpr std::array<std::uint8_t, 8u> unknown {0x06u, 0x10u, 0x02u, 0x08u, 0x00u, 0x08u, 0x07u, 0x21u};
        const auto heartbeat = connection::decode_connectionstate_response_packet(unknown);
        REQUIRE(heartbeat.has_value());
        CHECK(heartbeat->status == connect_status::connection_id);

        // The bus behind the server has gone away.
        std::array<std::uint8_t, 8u> encoded {};
        REQUIRE(connection::encode_disconnect_response_packet(encoded, disconnect_response_frame {7u, connect_status::knx_connection})
                    .has_value());
        CHECK(encoded == std::array<std::uint8_t, 8u> {0x06u, 0x10u, 0x02u, 0x0Au, 0x00u, 0x08u, 0x07u, 0x27u});
    }

    TEST_CASE("knx connection encoders reject unknown status values", "[knx][connection][unit]")
    {
        std::array<std::uint8_t, 8u> heartbeat {};
        const auto heartbeat_result = connection::encode_connectionstate_response_packet(
            heartbeat, connectionstate_response_frame {1u, static_cast<connect_status>(0xFFu)});
        REQUIRE(!heartbeat_result.has_value());
        CHECK(heartbeat_result.error() == make_error_code(error::invalid_configuration));

        std::array<std::uint8_t, 8u> disconnect {};
        const auto disconnect_result =
            connection::encode_disconnect_response_packet(disconnect, disconnect_response_frame {1u, static_cast<connect_status>(0xFFu)});
        REQUIRE(!disconnect_result.has_value());
        CHECK(disconnect_result.error() == make_error_code(error::invalid_configuration));
    }

    TEST_CASE("knx connection encoders take one defined host protocol for both endpoints", "[knx][connection][unit]")
    {
        // Endpoints on two protocols, or on one KNXnet/IP does not define, cannot describe a connection.
        const connect_request_frame mixed {
            .control_endpoint = hpai {control_address, 0x02u},
            .data_endpoint = hpai {data_address, 0x01u},
        };
        std::array<std::uint8_t, 26u> packet {};
        const auto mixed_result = connection::encode_connect_request_packet(packet, mixed);
        REQUIRE(!mixed_result.has_value());
        CHECK(mixed_result.error() == make_error_code(error::unsupported_hpai));
        const connect_request_frame undefined {
            .control_endpoint = hpai {control_address, 0x03u},
            .data_endpoint = hpai {data_address, 0x03u},
        };
        const auto undefined_result = connection::encode_connect_request_packet(packet, undefined);
        REQUIRE(!undefined_result.has_value());
        CHECK(undefined_result.error() == make_error_code(error::unsupported_hpai));

        // A response names a TCP data endpoint as readily as a UDP one, and nothing else.
        connect_response_frame response {
            .channel_id = 3u,
            .status = connect_status::no_error,
            .data_endpoint = hpai {{}, 0x02u},
        };
        std::array<std::uint8_t, 20u> response_packet {};
        REQUIRE(connection::encode_connect_response_packet(response_packet, response).has_value());
        const auto decoded = connection::decode_connect_response_packet(response_packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->data_endpoint.protocol == 0x02u);
        response.data_endpoint.protocol = 0x03u;
        const auto undefined_response = connection::encode_connect_response_packet(response_packet, response);
        REQUIRE(!undefined_response.has_value());
        CHECK(undefined_response.error() == make_error_code(error::unsupported_hpai));
    }

    TEST_CASE("knx connect request over TCP carries TCP HPAIs and the extended CRI", "[knx][connection][tcp][unit]")
    {
        // Over TCP both HPAIs are protocol 0x02 with address and port zero. The extended CRI appends the individual
        // address asked for - 1.1.5 here - and grows the information block from four octets to six.
        const connect_request_frame request {
            .control_endpoint = hpai {{}, 0x02u},
            .data_endpoint = hpai {{}, 0x02u},
            .requested_address = individual_address {1u, 1u, 5u},
        };
        std::array<std::uint8_t, 28u> packet {};
        REQUIRE(connection::encode_connect_request_packet(packet, request).has_value());
        constexpr std::array<std::uint8_t, 28u> expected {
            0x06u, 0x10u, 0x02u, 0x05u, 0x00u, 0x1Cu,               // header, 28 octets
            0x08u, 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, // control HPAI over TCP
            0x08u, 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, // data HPAI over TCP
            0x06u, 0x04u, 0x02u, 0x00u, 0x11u, 0x05u,               // extended CRI: tunnel, link layer, 1.1.5
        };
        CHECK(packet == expected);

        const auto decoded = connection::decode_connect_request_packet(expected);
        REQUIRE(decoded.has_value());
        CHECK(decoded->control_endpoint.protocol == 0x02u);
        CHECK(decoded->requested_address == individual_address {1u, 1u, 5u});
        const auto dispatched = decode_datagram(expected);
        REQUIRE(dispatched.has_value());
        const auto* const tunnelling = std::get_if<connect_request_frame>(&dispatched->payload);
        REQUIRE(tunnelling != nullptr);
        CHECK(tunnelling->requested_address == individual_address {1u, 1u, 5u});

        // A 28-octet request whose block claims four octets is not a tunnelling request, and a short buffer is refused.
        auto mislabelled = expected;
        mislabelled[22u] = 0x04u;
        const auto mislabelled_result = connection::decode_connect_request_packet(mislabelled);
        REQUIRE(!mislabelled_result.has_value());
        CHECK(mislabelled_result.error() == make_error_code(error::unsupported_connection_type));
        std::array<std::uint8_t, 26u> short_packet {};
        const auto short_result = connection::encode_connect_request_packet(short_packet, request);
        REQUIRE(!short_result.has_value());
        CHECK(short_result.error() == make_error_code(error::invalid_length));
    }

    // The second connection type a KNXnet/IP server offers. Its information block is two octets - length and
    // type - with no KNX layer and no assigned address, because nothing is tunnelled onto the bus.
    TEST_CASE("knx management connect request round-trips", "[knx][connection][unit]")
    {
        const management_connect_request_frame request {
            hpai {ipv4_endpoint {{192u, 0u, 2u, 1u}, 3671u}, 0x01u},
            hpai {ipv4_endpoint {{192u, 0u, 2u, 1u}, 3672u}, 0x01u},
        };

        std::array<std::uint8_t, frame::communication_header_size + connection::management_connect_request_body_size> packet {};
        REQUIRE(connection::encode_management_connect_request_packet(packet, request).has_value());
        CHECK(packet[2u] == 0x02u);
        CHECK(packet[3u] == 0x05u);
        CHECK(packet[22u] == 0x02u); // information block length
        CHECK(packet[23u] == 0x03u); // device management connection type

        const auto decoded = connection::decode_management_connect_request_packet(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->control_endpoint.endpoint.port == 3671u);
        CHECK(decoded->data_endpoint.endpoint.port == 3672u);
    }

    TEST_CASE("knx management connect response round-trips", "[knx][connection][unit]")
    {
        const management_connect_response_frame response {
            9u,
            connect_status::no_error,
            hpai {ipv4_endpoint {{192u, 0u, 2u, 20u}, 3671u}, 0x01u},
        };

        std::array<std::uint8_t, frame::communication_header_size + connection::management_connect_response_body_size> packet {};
        REQUIRE(connection::encode_management_connect_response_packet(packet, response).has_value());

        const auto decoded = connection::decode_management_connect_response_packet(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->channel_id == 9u);
        CHECK(decoded->status == connect_status::no_error);
        CHECK(decoded->data_endpoint.endpoint.address[3u] == 20u);
    }

    TEST_CASE("knx connect response decodes 8-byte error response", "[knx][connection][unit]")
    {
        // 06 10 | 02 06 | 00 08 | 00 (channel 0) | 24 (E_NO_MORE_CONNECTIONS)
        const std::array<std::uint8_t, 8u> error_packet {
            0x06u, 0x10u, 0x02u, 0x06u, 0x00u, 0x08u, 0x00u, 0x24u,
        };

        const auto decoded = connection::decode_connect_response_packet(error_packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->channel_id == 0u);
        CHECK(decoded->status == connect_status::no_more_connections);

        const auto mgmt_decoded = connection::decode_management_connect_response_packet(error_packet);
        REQUIRE(mgmt_decoded.has_value());
        CHECK(mgmt_decoded->channel_id == 0u);
        CHECK(mgmt_decoded->status == connect_status::no_more_connections);
    }

    TEST_CASE("knx connect request carries the requested knx layer", "[knx][connection][unit]")
    {
        const connect_request_frame request {
            hpai {ipv4_endpoint {{192u, 0u, 2u, 1u}, 3671u}, 0x01u},
            hpai {ipv4_endpoint {{192u, 0u, 2u, 1u}, 3672u}, 0x01u},
            connection::tunnel_busmonitor_layer,
        };

        std::array<std::uint8_t, frame::communication_header_size + connection::connect_request_body_size> packet {};
        REQUIRE(connection::encode_connect_request_packet(packet, request).has_value());
        CHECK(packet[24u] == 0x80u);

        const auto decoded = connection::decode_connect_request_packet(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->knx_layer == connection::tunnel_busmonitor_layer);

        // A layer nobody defines is refused rather than sent.
        auto rejected = request;
        rejected.knx_layer = 0x7Fu;
        CHECK(connection::encode_connect_request_packet(packet, rejected).error() == make_error_code(error::invalid_configuration));
    }
}
