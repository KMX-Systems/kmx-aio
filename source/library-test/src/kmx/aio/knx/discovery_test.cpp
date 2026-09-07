/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/datagram.hpp>
#include <kmx/aio/knx/discovery.hpp>
#include <kmx/aio/completion/executor.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace kmx::aio::test::knx::discovery_test
{
    using namespace kmx::aio::knx;

    class discovery_transport final: public datagram_transport
    {
    public:
        std::vector<std::uint8_t> response {};

        [[nodiscard]] task_returning_expected_size_t send(
            const cspan_byte_t payload, const sockaddr*, const ::socklen_t) noexcept(false) override
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
            const auto header = frame::decode_communication_header({bytes, payload.size()});
            if (!header.has_value())
                co_return std::unexpected(header.error());
            co_return expected_size_t {payload.size()};
        }

        [[nodiscard]] task_returning_expected_size_t receive(
            const span_byte_t buffer, transport_peer& peer) noexcept(false) override
        {
            peer.address = {};
            auto& address = reinterpret_cast<sockaddr_in6&>(peer.address);
            address.sin6_family = AF_INET6;
            address.sin6_port = htons(3671u);
            address.sin6_addr = in6addr_loopback;
            peer.length = sizeof(sockaddr_in6);
            for (std::size_t i = 0u; i < response.size(); ++i)
                buffer[i] = static_cast<std::byte>(response[i]);
            co_return expected_size_t {response.size()};
        }
    };

    TEST_CASE("knx search request round-trips discovery HPAI", "[knx][discovery][integration]")
    {
        const discovery::search_request_frame request {
            .discovery_endpoint = hpai { ipv4_endpoint { { 127u, 0u, 0u, 1u }, 3671u }, 0x01u },
        };
        std::array<std::uint8_t, 14u> packet {};

        REQUIRE(discovery::encode_search_request_packet(packet, request).has_value());
        const auto decoded = discovery::decode_search_request_packet(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->discovery_endpoint.endpoint.address[0] == 127u);
        CHECK(decoded->discovery_endpoint.endpoint.port == 3671u);
    }

    TEST_CASE("knx IPv6 search request round-trips discovery HPAI", "[knx][discovery][integration]")
    {
        const discovery::ipv6_search_request_frame request {
            .discovery_endpoint = ipv6_hpai {
                ipv6_endpoint {{0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u}, 3671u},
                0x01u,
            },
        };
        std::array<std::uint8_t, 26u> packet {};
        REQUIRE(discovery::encode_ipv6_search_request_packet(packet, request).has_value());
        const auto decoded = discovery::decode_ipv6_search_request_packet(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->discovery_endpoint.endpoint.address == request.discovery_endpoint.endpoint.address);
        CHECK(decoded->discovery_endpoint.endpoint.port == 3671u);
    }

    TEST_CASE("knx search request rejects a non-UDP HPAI", "[knx][discovery][unit]")
    {
        const std::array<std::uint8_t, 14u> packet {
            0x06u, 0x10u, 0x02u, 0x01u, 0x00u, 0x0Eu,
            0x08u, 0x02u, 127u, 0u, 0u, 1u, 0x0Eu, 0x57u,
        };

        const auto decoded = discovery::decode_search_request_packet(packet);
        REQUIRE(!decoded.has_value());
        CHECK(decoded.error() == make_error_code(error::unsupported_hpai));
    }

    TEST_CASE("knx search response round-trips opaque device information", "[knx][discovery][integration]")
    {
        const discovery::search_response_frame response {
            .control_endpoint = hpai { ipv4_endpoint { { 127u, 0u, 0u, 1u }, 3671u }, 0x01u },
            .device_info_blocks = { 0x04u, 0x02u, 0x01u, 0x00u },
        };
        std::array<std::uint8_t, 18u> packet {};
        REQUIRE(discovery::encode_search_response_packet(packet, response).has_value());

        const auto decoded = discovery::decode_search_response_packet(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->control_endpoint.endpoint.port == 3671u);
        CHECK(decoded->device_info_blocks == response.device_info_blocks);

        std::array<std::uint8_t, 18u> reencoded {};
        const datagram value { discovery::search_response_service, decoded.value() };
        REQUIRE(encode_datagram(reencoded, value).has_value());
        CHECK(reencoded == packet);
    }

    TEST_CASE("knx IPv6 discovery client returns a typed search response", "[knx][discovery][integration]")
    {
        discovery_transport transport;
        const discovery::ipv6_search_response_frame response {
            .control_endpoint = ipv6_hpai {
                ipv6_endpoint {{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u}, 3672u},
                0x01u,
            },
            .device_info_blocks = {0x04u, 0x02u, 0x01u, 0x00u},
        };
        transport.response.resize(30u);
        REQUIRE(discovery::encode_ipv6_search_response_packet(transport.response, response).has_value());

        sockaddr_storage peer {};
        auto& address = reinterpret_cast<sockaddr_in6&>(peer);
        address.sin6_family = AF_INET6;
        address.sin6_port = htons(3671u);
        address.sin6_addr = in6addr_loopback;
        bool succeeded = false;
        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            discovery::client client {transport, peer, sizeof(sockaddr_in6)};
            const auto result = co_await client.search(discovery::ipv6_search_request_frame {
                .discovery_endpoint = ipv6_hpai {
                    ipv6_endpoint {{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u}, 3671u},
                    0x01u,
                },
            });
            succeeded = result.has_value() && std::holds_alternative<discovery::ipv6_search_response_frame>(*result);
            executor.stop();
        };
        executor.spawn(run());
        executor.run();
        CHECK(succeeded);
    }

    TEST_CASE("knx IPv6 search response round-trips opaque device information", "[knx][discovery][integration]")
    {
        const discovery::ipv6_search_response_frame response {
            .control_endpoint = ipv6_hpai {
                ipv6_endpoint {{15u, 14u, 13u, 12u, 11u, 10u, 9u, 8u, 7u, 6u, 5u, 4u, 3u, 2u, 1u, 0u}, 3671u},
                0x01u,
            },
            .device_info_blocks = {0x04u, 0x02u, 0x01u, 0x00u},
        };
        std::array<std::uint8_t, 30u> packet {};
        REQUIRE(discovery::encode_ipv6_search_response_packet(packet, response).has_value());
        const auto decoded = discovery::decode_ipv6_search_response_packet(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->control_endpoint.endpoint.address == response.control_endpoint.endpoint.address);
        CHECK(decoded->control_endpoint.endpoint.port == 3671u);
        CHECK(decoded->device_info_blocks == response.device_info_blocks);
    }

    TEST_CASE("knx search response rejects an invalid DIB length", "[knx][discovery][unit]")
    {
        const std::array<std::uint8_t, 16u> packet {
            0x06u, 0x10u, 0x02u, 0x02u, 0x00u, 0x10u,
            0x08u, 0x01u, 127u, 0u, 0u, 1u, 0x0Eu, 0x57u,
            0x08u, 0x02u,
        };
        const auto decoded = discovery::decode_search_response_packet(packet);
        REQUIRE(!decoded.has_value());
        CHECK(decoded.error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx discovery rejects IPv6 search HPAI as unsupported", "[knx][discovery][unit]")
    {
        const std::array<std::uint8_t, 14u> packet {
            0x06u, 0x10u, 0x02u, 0x01u, 0x00u, 0x0Eu,
            0x14u, 0x01u, 127u, 0u, 0u, 1u, 0x0Eu, 0x57u,
        };
        const auto decoded = discovery::decode_search_request_packet(packet);
        REQUIRE(!decoded.has_value());
        CHECK(decoded.error() == make_error_code(error::unsupported_hpai));
    }

    TEST_CASE("knx discovery encoders reject unsupported HPAI protocols", "[knx][discovery][unit]")
    {
        const discovery::search_request_frame request {
            .discovery_endpoint = hpai { ipv4_endpoint { { 127u, 0u, 0u, 1u }, 3671u }, 0x02u },
        };
        std::array<std::uint8_t, 14u> packet {};
        const auto result = discovery::encode_search_request_packet(packet, request);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::unsupported_hpai));
    }

    TEST_CASE("knx description request and response round-trip", "[knx][discovery][integration]")
    {
        std::array<std::uint8_t, 6u> request_packet {};
        REQUIRE(discovery::encode_description_request_packet(request_packet).has_value());
        const auto request = discovery::decode_description_request_packet(request_packet);
        REQUIRE(request.has_value());

        const datagram request_datagram { discovery::description_request_service, request.value() };
        std::array<std::uint8_t, 6u> reencoded_request {};
        REQUIRE(encode_datagram(reencoded_request, request_datagram).has_value());
        CHECK(reencoded_request == request_packet);

        const discovery::description_response_frame response {
            .device_info_blocks = { 0x04u, 0x02u, 0x01u, 0x00u },
        };
        std::array<std::uint8_t, 10u> response_packet {};
        REQUIRE(discovery::encode_description_response_packet(response_packet, response).has_value());
        const auto decoded_response = discovery::decode_description_response_packet(response_packet);
        REQUIRE(decoded_response.has_value());
        CHECK(decoded_response->device_info_blocks == response.device_info_blocks);
    }

    TEST_CASE("knx discovery responses preserve multiple DIBs", "[knx][discovery][integration]")
    {
        const discovery::search_response_frame response {
            .control_endpoint = hpai { ipv4_endpoint { { 127u, 0u, 0u, 1u }, 3671u }, 0x01u },
            .device_info_blocks = { 0x02u, 0x01u, 0x04u, 0x02u, 0xAAu, 0xBBu },
        };
        std::array<std::uint8_t, 20u> packet {};
        REQUIRE(discovery::encode_search_response_packet(packet, response).has_value());
        const auto decoded = discovery::decode_search_response_packet(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->device_info_blocks == response.device_info_blocks);
    }

    TEST_CASE("knx discovery responses reject a truncated trailing DIB", "[knx][discovery][unit]")
    {
        const std::array<std::uint8_t, 18u> packet {
            0x06u, 0x10u, 0x02u, 0x02u, 0x00u, 0x12u,
            0x08u, 0x01u, 127u, 0u, 0u, 1u, 0x0Eu, 0x57u,
            0x02u, 0x01u, 0x05u, 0xAAu,
        };
        const auto decoded = discovery::decode_search_response_packet(packet);
        REQUIRE(!decoded.has_value());
        CHECK(decoded.error() == make_error_code(error::malformed_frame));
    }
}
