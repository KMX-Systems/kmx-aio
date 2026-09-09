/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/datagram.hpp>
#include <kmx/aio/knx/discovery.hpp>
#include <kmx/aio/completion/executor.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <variant>
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

    namespace detail
    {
        /// @brief Issues one IPv6 SEARCH and records whether the typed response came back.
        task<void> search_ipv6(datagram_transport& transport, const sockaddr_storage& peer, bool& succeeded,
                               completion::executor& executor) noexcept(false)
        {
            discovery::client client {transport, peer, sizeof(sockaddr_in6)};
            const auto result = co_await client.search(discovery::ipv6_search_request_frame {
                .discovery_endpoint =
                    ipv6_hpai {
                        ipv6_endpoint {{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u}, 3671u},
                        0x01u,
                    },
            });
            succeeded = result.has_value() && std::holds_alternative<discovery::ipv6_search_response_frame>(*result);
            executor.stop();
        }
    } // namespace detail

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
        bool succeeded {};
        completion::executor executor;
        executor.spawn(detail::search_ipv6(transport, peer, succeeded, executor));
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


    namespace detail
    {
        /// @brief A transport that replays a queue of datagrams, each from its own source address.
        /// @details A discovery search is answered by several servers, so a transport that can only speak
        ///          for one of them cannot exercise the collecting path at all.
        class multi_response_transport final: public datagram_transport
        {
        public:
            struct answer
            {
                std::vector<std::uint8_t> bytes {};
                std::uint8_t last_address_octet {};
            };

            std::vector<answer> answers {};
            std::size_t next {};
            std::size_t sends {};

            [[nodiscard]] task_returning_expected_size_t send(const cspan_byte_t payload, const sockaddr*,
                                                               const ::socklen_t) noexcept(false) override
            {
                ++sends;
                co_return expected_size_t {payload.size()};
            }

            [[nodiscard]] task_returning_expected_size_t receive(const span_byte_t buffer,
                                                                 transport_peer& peer) noexcept(false) override
            {
                if (next >= answers.size())
                    co_return std::unexpected(make_error_code(error::timeout));

                const auto& value = answers[next++];
                peer.address = {};
                auto& address = reinterpret_cast<sockaddr_in&>(peer.address);
                address.sin_family = AF_INET;
                address.sin_port = htons(3671u);
                address.sin_addr.s_addr = htonl(0xC0000200u | value.last_address_octet); // 192.0.2.x
                peer.length = sizeof(sockaddr_in);

                for (std::size_t i = 0u; i < value.bytes.size(); ++i)
                    buffer[i] = static_cast<std::byte>(value.bytes[i]);
                co_return expected_size_t {value.bytes.size()};
            }

            [[nodiscard]] task_returning_expected_size_t receive_until(const span_byte_t buffer, transport_peer& peer,
                                                                       std::uint32_t) noexcept(false) override
            {
                co_return co_await receive(buffer, peer);
            }
        };

        /// @brief Builds a SEARCH_RESPONSE naming one control endpoint.
        [[nodiscard]] inline std::vector<std::uint8_t> search_response_bytes(const std::uint8_t last_octet)
        {
            const discovery::search_response_frame response {
                hpai {ipv4_endpoint {{192u, 0u, 2u, last_octet}, 3671u}, 0x01u},
                {0x04u, 0x02u, 0x01u, 0x00u},
            };
            std::vector<std::uint8_t> packet(frame::communication_header_size + connection::hpai_size + 4u, 0u);
            REQUIRE(discovery::encode_search_response_packet(packet, response).has_value());
            return packet;
        }

        /// @brief The KNXnet/IP discovery multicast group, 224.0.23.12:3671.
        [[nodiscard]] inline sockaddr_in discovery_group() noexcept
        {
            sockaddr_in address {};
            address.sin_family = AF_INET;
            address.sin_port = htons(3671u);
            address.sin_addr.s_addr = htonl(0xE000170Cu);
            return address;
        }
    }

    // A search sent to 224.0.23.12 is answered by each server from its own unicast address. Requiring the
    // answer to come from the address the request went to rejected every one of them, which no unicast
    // loopback test could show.
    TEST_CASE("knx discovery collects every answer to a multicast search", "[knx][discovery][integration]")
    {
        detail::multi_response_transport transport;
        transport.answers = {
            {detail::search_response_bytes(10u), 10u},
            {detail::search_response_bytes(11u), 11u},
        };

        auto group = detail::discovery_group();
        discovery::client client {transport, reinterpret_cast<const sockaddr*>(&group), sizeof(group)};

        std::vector<discovery::discovered_server> found;
        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            auto result = co_await client.search_all(discovery::search_request_frame {
                hpai {ipv4_endpoint {{192u, 0u, 2u, 1u}, 3672u}, 0x01u},
            });
            if (result.has_value())
                found = std::move(result.value());
            executor.stop();
        };
        executor.spawn(run());
        executor.run();

        CHECK(transport.sends == 1u);
        REQUIRE(found.size() == 2u);
        CHECK(reinterpret_cast<const sockaddr_in&>(found[0u].source).sin_addr.s_addr == htonl(0xC000020Au));
        CHECK(reinterpret_cast<const sockaddr_in&>(found[1u].source).sin_addr.s_addr == htonl(0xC000020Bu));
        REQUIRE(std::holds_alternative<discovery::search_response_frame>(found[0u].response));
        CHECK(std::get<discovery::search_response_frame>(found[0u].response).control_endpoint.endpoint.address[3u] == 10u);
    }

    TEST_CASE("knx discovery search_all ends quietly when nothing answers", "[knx][discovery][unit]")
    {
        detail::multi_response_transport transport;
        auto group = detail::discovery_group();
        discovery::client client {transport, reinterpret_cast<const sockaddr*>(&group), sizeof(group)};

        bool empty {};
        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            const auto result = co_await client.search_all(discovery::search_request_frame {
                hpai {ipv4_endpoint {{192u, 0u, 2u, 1u}, 3672u}, 0x01u},
            });
            empty = result.has_value() && result->empty();
            executor.stop();
        };
        executor.spawn(run());
        executor.run();
        CHECK(empty);
    }

    TEST_CASE("knx discovery describe returns the device description", "[knx][discovery][integration]")
    {
        const discovery::description_response_frame response {.device_info_blocks = {0x04u, 0x02u, 0x01u, 0x00u}};
        std::vector<std::uint8_t> response_packet(frame::communication_header_size + 4u, 0u);
        REQUIRE(discovery::encode_description_response_packet(response_packet, response).has_value());

        detail::multi_response_transport transport;
        transport.answers = {{response_packet, 10u}};

        sockaddr_in server {};
        server.sin_family = AF_INET;
        server.sin_port = htons(3671u);
        server.sin_addr.s_addr = htonl(0xC000020Au); // 192.0.2.10, the source the transport answers from
        discovery::client client {transport, reinterpret_cast<const sockaddr*>(&server), sizeof(server)};

        bool described {};
        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            const auto result = co_await client.describe(discovery::description_request_frame {
                hpai {ipv4_endpoint {{192u, 0u, 2u, 1u}, 3672u}, 0x01u},
            });
            described = result.has_value() && (result->device_info_blocks == byte_buffer_t {0x04u, 0x02u, 0x01u, 0x00u});
            executor.stop();
        };
        executor.spawn(run());
        executor.run();
        CHECK(described);
    }

    TEST_CASE("knx description request and response round-trip", "[knx][discovery][integration]")
    {
        // A DESCRIPTION_REQUEST carries the control endpoint the answer is to be sent to; a header-only
        // request names nowhere to reply and is rejected by every server.
        const discovery::description_request_frame source_request {
            hpai {ipv4_endpoint {{192u, 0u, 2u, 5u}, 3672u}, 0x01u},
        };
        std::array<std::uint8_t, frame::communication_header_size + discovery::description_request_body_size> request_packet {};
        REQUIRE(discovery::encode_description_request_packet(request_packet, source_request).has_value());
        CHECK(request_packet == std::array<std::uint8_t, 14u> {
                                    0x06u, 0x10u, 0x02u, 0x03u, 0x00u, 0x0Eu,
                                    0x08u, 0x01u, 192u, 0u, 2u, 5u, 0x0Eu, 0x58u,
                                });

        const auto request = discovery::decode_description_request_packet(request_packet);
        REQUIRE(request.has_value());
        CHECK(request->control_endpoint.endpoint.port == 3672u);

        const datagram request_datagram { discovery::description_request_service, request.value() };
        std::array<std::uint8_t, request_packet.size()> reencoded_request {};
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

    // SEARCH_REQUEST_EXTENDED narrows a search with parameter blocks. Each is [length][mandatory|type][data]
    // and the length counts its own two octets, so a block that does not advance the cursor would loop.
    TEST_CASE("knx extended search request round-trips its parameters", "[knx][discovery][unit]")
    {
        const discovery::extended_search_request_frame request {
            hpai {ipv4_endpoint {{192u, 0u, 2u, 1u}, 3672u}, 0x01u},
            {
                discovery::search_parameter {true, discovery::search_parameter_type::programming_mode, {}},
                discovery::search_parameter {true, discovery::search_parameter_type::mac_address,
                                             {0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u}},
            },
        };

        const auto size = discovery::extended_search_request_size(request);
        REQUIRE(size.has_value());
        CHECK(*size == 6u + 8u + 2u + 8u);

        const std::array<std::uint8_t, 24u> expected {
            0x06u, 0x10u, 0x02u, 0x0Bu, 0x00u, 0x18u,
            0x08u, 0x01u, 192u, 0u, 2u, 1u, 0x0Eu, 0x58u,
            0x02u, 0x81u,                                        // programming mode, mandatory
            0x08u, 0x82u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, // select by MAC, mandatory
        };

        std::vector<std::uint8_t> encoded(*size, 0u);
        REQUIRE(discovery::encode_extended_search_request_packet(encoded, request).has_value());
        CHECK(std::equal(encoded.begin(), encoded.end(), expected.begin()));

        const auto decoded = discovery::decode_extended_search_request_packet(encoded);
        REQUIRE(decoded.has_value());
        CHECK(decoded->discovery_endpoint.endpoint.port == 3672u);
        REQUIRE(decoded->parameters.size() == 2u);
        CHECK(decoded->parameters[0u].type == discovery::search_parameter_type::programming_mode);
        CHECK(decoded->parameters[0u].mandatory);
        CHECK(decoded->parameters[0u].data.empty());
        CHECK(decoded->parameters[1u].type == discovery::search_parameter_type::mac_address);
        CHECK(decoded->parameters[1u].data.size() == 6u);
    }

    TEST_CASE("knx extended search request pads an odd parameter block", "[knx][discovery][unit]")
    {
        // A DIB request list of one octet makes the block odd, and a parameter block is an even number of
        // octets, so the encoder pads it.
        const discovery::extended_search_request_frame request {
            hpai {ipv4_endpoint {{192u, 0u, 2u, 1u}, 3672u}, 0x01u},
            {discovery::search_parameter {false, discovery::search_parameter_type::request_dibs, {0x01u}}},
        };

        const auto size = discovery::extended_search_request_size(request);
        REQUIRE(size.has_value());
        CHECK(*size == 6u + 8u + 4u);

        std::vector<std::uint8_t> encoded(*size, 0xFFu);
        REQUIRE(discovery::encode_extended_search_request_packet(encoded, request).has_value());
        CHECK(encoded[14u] == 0x04u); // block length, padded
        CHECK(encoded[15u] == 0x04u); // not mandatory, so no high bit
        CHECK(encoded[16u] == 0x01u);
        CHECK(encoded[17u] == 0x00u); // the pad octet
    }

    TEST_CASE("knx extended search request rejects malformed parameters", "[knx][discovery][unit]")
    {
        // A block length below the two-octet header would never advance the decode cursor.
        std::vector<std::uint8_t> packet {
            0x06u, 0x10u, 0x02u, 0x0Bu, 0x00u, 0x10u,
            0x08u, 0x01u, 192u, 0u, 2u, 1u, 0x0Eu, 0x58u,
            0x01u, 0x81u,
        };
        CHECK(discovery::decode_extended_search_request_packet(packet).error() == make_error_code(error::malformed_frame));

        // A select-by-MAC block whose length does not match the six octets a MAC address has.
        packet[5u] = 0x12u;
        packet[14u] = 0x04u;
        packet[15u] = 0x82u;
        packet.push_back(0x01u);
        packet.push_back(0x02u);
        CHECK(discovery::decode_extended_search_request_packet(packet).error() == make_error_code(error::malformed_frame));

        // An unknown optional parameter is skipped without error.
        std::vector<std::uint8_t> optional_packet {
            0x06u, 0x10u, 0x02u, 0x0Bu, 0x00u, 0x12u,
            0x08u, 0x01u, 192u, 0u, 2u, 1u, 0x0Eu, 0x58u,
            0x04u, 0x7Eu, 0xAAu, 0x00u, // type 0x7E, mandatory bit 0, size 4
        };
        const auto decoded_opt = discovery::decode_extended_search_request_packet(optional_packet);
        REQUIRE(decoded_opt.has_value());
        CHECK(decoded_opt->parameters.empty());

        // An unknown mandatory parameter is rejected as unsupported.
        optional_packet[15u] = 0xFEu; // mandatory bit 1
        CHECK(discovery::decode_extended_search_request_packet(optional_packet).error() ==
              make_error_code(error::unsupported_service));
    }

    TEST_CASE("knx extended search response round-trips", "[knx][discovery][unit]")
    {
        const discovery::extended_search_response_frame response {
            hpai {ipv4_endpoint {{192u, 0u, 2u, 20u}, 3671u}, 0x01u},
            {0x04u, 0x02u, 0x01u, 0x00u},
        };

        std::array<std::uint8_t, 6u + 8u + 4u> encoded {};
        REQUIRE(discovery::encode_extended_search_response_packet(encoded, response).has_value());
        CHECK(encoded[2u] == 0x02u);
        CHECK(encoded[3u] == 0x0Cu);

        const auto decoded = discovery::decode_extended_search_response_packet(encoded);
        REQUIRE(decoded.has_value());
        CHECK(decoded->control_endpoint.endpoint.address[3u] == 20u);
        CHECK(decoded->device_info_blocks == byte_buffer_t {0x04u, 0x02u, 0x01u, 0x00u});

        // And through the typed dispatcher, which is the route a received datagram takes.
        const auto dispatched = decode_datagram(encoded);
        REQUIRE(dispatched.has_value());
        CHECK(dispatched->service_type == discovery::search_response_extended_service);
        CHECK(std::holds_alternative<discovery::extended_search_response_frame>(dispatched->payload));
    }
}
