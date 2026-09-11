/// @file src/kmx/aio/knx/generic_server_test.cpp
/// @brief Unit tests for the KNXnet/IP tunnelling server: channel lifecycle and expiry, search and description answers.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/generic_server.hpp>
#ifndef PCH
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/knx/connection.hpp>
    #include <kmx/aio/knx/dib.hpp>
    #include <kmx/aio/knx/dib/device_info.hpp>
    #include <kmx/aio/knx/dib/supported_service_families.hpp>
    #include <kmx/aio/knx/discovery.hpp>
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/frame.hpp>
    #include <kmx/aio/knx/individual_address.hpp>
    #include <kmx/aio/knx/transport.hpp>
    #include <kmx/aio/task.hpp>
    #include <kmx/aio/test/knx/recording_transport.hpp>
    #include <kmx/aio/test/knx/telegram.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <cstring>
    #include <deque>
    #include <stop_token>
    #include <variant>
    #include <vector>
    #include <netinet/in.h>
#endif

namespace kmx::aio::test::knx::generic_server_test
{
    using namespace kmx::aio::knx;

    namespace detail
    {
        /// @brief The time the server under test reads.
        std::uint32_t now_ms {};

        /// @brief Reads @ref now_ms.
        [[nodiscard]] std::uint32_t clock_now() noexcept
        {
            return now_ms;
        }

        class scripted_transport final: public test::knx::recording_transport
        {
        public:
            bool ipv6_peer {};
            std::uint16_t receive_port = 40000u;
            std::uint32_t receive_address = 0x7F000001u;
            std::uint32_t timeout_receives {};
            std::deque<std::vector<std::uint8_t>> incoming {};

            /// @brief The packets this transport was asked to send; a spelling of @ref sent_packets.
            [[nodiscard]] const std::vector<std::vector<std::uint8_t>>& outgoing() const noexcept { return sent_packets(); }
            /// @brief The peers those packets were addressed to; a spelling of @ref sent_peers.
            [[nodiscard]] const std::vector<sockaddr_storage>& outgoing_peers() const noexcept { return sent_peers(); }

            [[nodiscard]] task_returning_expected_size_t send(const cspan_byte_t payload, const sockaddr* peer,
                                                              const ::socklen_t peer_length) noexcept(false) override
            {
                record_send(payload, peer, peer_length);
                co_return expected_size_t {payload.size()};
            }

            [[nodiscard]] task_returning_expected_size_t receive(const span_byte_t buffer, transport_peer& peer) noexcept(false) override
            {
                // A configured run of timeouts comes first, so a test can make the server wait before it
                // ever sees a datagram.
                if (timeout_receives != 0u)
                {
                    --timeout_receives;
                    co_return std::unexpected(make_error_code(error::timeout));
                }

                if (incoming.empty())
                    co_return std::unexpected(make_error_code(error::timeout));

                const auto packet = std::move(incoming.front());
                incoming.pop_front();

                fill_peer(peer, ipv6_peer, receive_address, receive_port);
                co_return deliver(packet, buffer);
            }
        };

        /// @brief Serves one CONNECT_REQUEST and records the channel the server handed out.
        task<void> serve_connect(generic_server& server, bool& connected, std::uint8_t& channel_id,
                                 completion::executor& executor) noexcept(false)
        {
            const auto result = co_await server.serve_once();
            connected = result.has_value() && result->cemi_bytes.empty();
            channel_id = result ? result->channel_id : 0u;
            executor.stop();
        }

        /// @brief Serves one TUNNELLING_REQUEST and records whether the expected cEMI arrived.
        task<void> serve_tunnelling(generic_server& server, const std::uint8_t channel_id, bool& received,
                                    completion::executor& tunnel_executor) noexcept(false)
        {
            const auto result = co_await server.serve_once();
            received = result.has_value() && result->channel_id == channel_id &&
                       result->cemi_bytes == std::vector<std::uint8_t>(sample_cemi.begin(), sample_cemi.end());
            tunnel_executor.stop();
        }

        /// @brief Asks the stop source to stop, then serves and records the cancellation.
        task<void> serve_until_cancelled(generic_server& server, std::stop_source& stop_source, bool& cancelled,
                                         completion::executor& executor) noexcept(false)
        {
            stop_source.request_stop();
            const auto result = co_await server.serve();
            cancelled = !result.has_value() && result.error() == make_error_code(error::shutdown);
            executor.stop();
        }

        /// @brief How many datagrams a serving loop takes, and the failure it counts among them.
        struct serving_plan
        {
            /// @brief How many datagrams to serve.
            std::size_t count {};
            /// @brief The failure to count.
            std::error_code expected {};
        };

        /// @brief Serves the planned number of datagrams and counts how many of them failed as the plan expects.
        task<void> serve_counting(generic_server& server, const serving_plan plan, std::size_t& matched,
                                  completion::executor& executor) noexcept(false)
        {
            for (std::size_t index {}; index < plan.count; ++index)
            {
                const auto result = co_await server.serve_once();
                if (!result.has_value() && (result.error() == plan.expected))
                    ++matched;
            }

            executor.stop();
        }
    }

    TEST_CASE("knx server allocates channel and handles tunnelling lifecycle", "[knx][server][integration]")
    {
        detail::scripted_transport transport;
        generic_server server {transport, server_config {.max_channels = 2u, .first_assigned_address = individual_address {1u, 1u, 10u}}};

        const connect_request_frame connect {
            .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
            .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 2u}, 40001u}, 0x01u},
        };
        std::vector<std::uint8_t> connect_packet(26u);
        REQUIRE(connection::encode_connect_request_packet(connect_packet, connect).has_value());
        transport.incoming.push_back(connect_packet);

        bool connected {};
        std::uint8_t channel_id {};
        completion::executor executor;
        executor.spawn(detail::serve_connect(server, connected, channel_id, executor));
        executor.run();

        REQUIRE(connected);
        CHECK(channel_id != 0u);
        CHECK(server.channel_active(channel_id));
        REQUIRE(transport.sent_packets().size() == 1u);
        const auto response = connection::decode_connect_response_packet(transport.sent_packets().front());
        REQUIRE(response.has_value());
        CHECK(response->channel_id == channel_id);
        CHECK(response->assigned_address == individual_address {1u, 1u, 10u});

        transport.clear_sent_packets();
        transport.receive_port = 40001u;
        transport.receive_address = 0x7F000002u;
        std::vector<std::uint8_t> request(6u + 4u + sample_cemi.size());
        REQUIRE(frame::encode_tunnelling_request_packet(request, channel_id, 0u, sample_cemi).has_value());
        transport.incoming.push_back(request);

        bool received {};
        completion::executor tunnel_executor;
        tunnel_executor.spawn(detail::serve_tunnelling(server, channel_id, received, tunnel_executor));
        tunnel_executor.run();
        CHECK(received);
        REQUIRE(transport.sent_packets().size() == 1u);
        CHECK(frame::decode_tunnelling_ack_packet(transport.sent_packets().front()).has_value());
        REQUIRE(transport.sent_peers().size() >= 2u);
        CHECK(ntohs(reinterpret_cast<const sockaddr_in&>(transport.sent_peers()[1u]).sin_port) == 40001u);
        CHECK(reinterpret_cast<const sockaddr_in&>(transport.sent_peers()[1u]).sin_addr.s_addr == htonl(0x7F000002u));

        transport.clear_sent_packets();
        transport.incoming.push_back(request);
        bool duplicate_delivered = true;
        completion::executor duplicate_executor;
        auto duplicate_task = [&]() -> task<void>
        {
            const auto result = co_await server.serve_once();
            duplicate_delivered = result.has_value() && !result->cemi_bytes.empty();
            duplicate_executor.stop();
        };
        duplicate_executor.spawn(duplicate_task());
        duplicate_executor.run();
        CHECK(!duplicate_delivered);
        REQUIRE(transport.sent_packets().size() == 1u);
        CHECK(frame::decode_tunnelling_ack_packet(transport.sent_packets().front()).has_value());

        std::vector<std::uint8_t> out_of_order(6u + 4u + sample_cemi.size());
        REQUIRE(frame::encode_tunnelling_request_packet(out_of_order, channel_id, 9u, sample_cemi).has_value());
        transport.incoming.push_back(out_of_order);
        bool rejected {};
        completion::executor order_executor;
        auto order_task = [&]() -> task<void>
        {
            const auto result = co_await server.serve_once();
            rejected = !result.has_value() && result.error() == make_error_code(error::sequence_error);
            order_executor.stop();
        };
        order_executor.spawn(order_task());
        order_executor.run();
        CHECK(rejected);

        REQUIRE(server.disconnect(channel_id).has_value());
        CHECK(!server.channel_active(channel_id));
    }

    TEST_CASE("knx server rejects a connection when channel capacity is exhausted", "[knx][server][unit]")
    {
        detail::scripted_transport transport;
        generic_server server {transport, server_config {.max_channels = 1u}};
        const connect_request_frame connect {
            .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
            .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
        };
        std::vector<std::uint8_t> packet(26u);
        REQUIRE(connection::encode_connect_request_packet(packet, connect).has_value());
        transport.incoming.push_back(packet);

        completion::executor first_executor;
        bool first_connected {};
        auto first = [&]() -> task<void>
        {
            first_connected = (co_await server.serve_once()).has_value();
            first_executor.stop();
        };
        first_executor.spawn(first());
        first_executor.run();
        REQUIRE(first_connected);

        transport.incoming.push_back(packet);
        completion::executor second_executor;
        bool exhausted {};
        auto second = [&]() -> task<void>
        {
            const auto result = co_await server.serve_once();
            exhausted = !result.has_value() && result.error() == make_error_code(error::send_queue_full);
            second_executor.stop();
        };
        second_executor.spawn(second());
        second_executor.run();

        CHECK(exhausted);
        CHECK(server.active_channels() == 1u);
        REQUIRE(server.shutdown().has_value());
        CHECK(server.active_channels() == 0u);
    }

    // A heartbeat or a disconnect naming a channel the server does not hold is answered, not ignored:
    // E_CONNECTION_ID is what makes a client whose channel was reclaimed reconnect at once instead of after
    // three unanswered heartbeats.
    TEST_CASE("knx server answers an unknown channel with E_CONNECTION_ID", "[knx][server][unit]")
    {
        detail::scripted_transport transport;
        generic_server server {transport};

        std::array<std::uint8_t, 16u> heartbeat {};
        REQUIRE(connection::encode_connectionstate_request_packet(heartbeat, connectionstate_request_frame {7u}).has_value());
        transport.incoming.emplace_back(heartbeat.begin(), heartbeat.end());
        std::array<std::uint8_t, 16u> disconnect {};
        REQUIRE(connection::encode_disconnect_request_packet(disconnect, disconnect_request_frame {7u}).has_value());
        transport.incoming.emplace_back(disconnect.begin(), disconnect.end());

        std::size_t refused {};
        completion::executor executor;
        executor.spawn(detail::serve_counting(server, {2u, make_error_code(error::sequence_error)}, refused, executor));
        executor.run();

        CHECK(refused == 2u);
        CHECK(server.active_channels() == 0u);
        REQUIRE(transport.sent_packets().size() == 2u);
        const auto heartbeat_response = connection::decode_connectionstate_response_packet(transport.sent_packets()[0u]);
        REQUIRE(heartbeat_response.has_value());
        CHECK(heartbeat_response->channel_id == 7u);
        CHECK(heartbeat_response->status == connect_status::connection_id);
        const auto disconnect_response = connection::decode_disconnect_response_packet(transport.sent_packets()[1u]);
        REQUIRE(disconnect_response.has_value());
        CHECK(disconnect_response->status == connect_status::connection_id);
    }

    TEST_CASE("knx server accepts an IPv6 CONNECT request", "[knx][server][integration][ipv6]")
    {
        detail::scripted_transport transport;
        transport.ipv6_peer = true;
        generic_server server {transport};
        const ipv6_connect_request_frame connect {
            .control_endpoint = ipv6_hpai {ipv6_endpoint {{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u}, 40000u}, 0x01u},
            .data_endpoint = ipv6_hpai {ipv6_endpoint {{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u}, 40001u}, 0x01u},
        };
        std::vector<std::uint8_t> packet(50u);
        REQUIRE(connection::encode_ipv6_connect_request_packet(packet, connect).has_value());
        transport.incoming.push_back(packet);

        bool accepted {};
        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            const auto result = co_await server.serve_once();
            accepted = result.has_value();
            executor.stop();
        };
        executor.spawn(run());
        executor.run();

        REQUIRE(accepted);
        REQUIRE(transport.sent_packets().size() == 1u);
        const auto response = connection::decode_ipv6_connect_response_packet(transport.sent_packets().front());
        REQUIRE(response.has_value());
        CHECK(response->channel_id != 0u);
    }

    TEST_CASE("knx server expires inactive channels", "[knx][server][unit]")
    {
        detail::now_ms = 100u;
        detail::scripted_transport transport;
        generic_server server {
            transport,
            server_config {.max_channels = 1u, .inactivity_timeout_ms = 50u},
            {.clock_now = &detail::clock_now},
        };
        const connect_request_frame connect {
            .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
            .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
        };
        std::vector<std::uint8_t> packet(26u);
        REQUIRE(connection::encode_connect_request_packet(packet, connect).has_value());
        transport.incoming.push_back(packet);

        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            REQUIRE((co_await server.serve_once()).has_value());
            executor.stop();
        };
        executor.spawn(run());
        executor.run();
        REQUIRE(server.active_channels() == 1u);

        detail::now_ms = 151u;
        REQUIRE(server.poll().has_value());
        CHECK(server.active_channels() == 0u);
        detail::now_ms = 0u;
    }

    TEST_CASE("knx server polls stale channels before serving next packet", "[knx][server][integration]")
    {
        detail::now_ms = 100u;
        detail::scripted_transport transport;
        generic_server server {
            transport,
            server_config {.max_channels = 1u, .inactivity_timeout_ms = 50u},
            {.clock_now = &detail::clock_now},
        };
        const connect_request_frame connect {
            .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
            .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
        };
        std::vector<std::uint8_t> packet(26u);
        REQUIRE(connection::encode_connect_request_packet(packet, connect).has_value());
        transport.incoming.push_back(packet);

        completion::executor first_executor;
        auto first = [&]() -> task<void>
        {
            REQUIRE((co_await server.serve_once()).has_value());
            first_executor.stop();
        };
        first_executor.spawn(first());
        first_executor.run();
        REQUIRE(server.active_channels() == 1u);

        detail::now_ms = 151u;
        transport.incoming.push_back(packet);
        completion::executor second_executor;
        bool accepted {};
        auto second = [&]() -> task<void>
        {
            accepted = (co_await server.serve_once()).has_value();
            second_executor.stop();
        };
        second_executor.spawn(second());
        second_executor.run();
        CHECK(accepted);
        CHECK(server.active_channels() == 1u);
        detail::now_ms = 0u;
    }

    TEST_CASE("knx server serve retries timeout and honors cancellation", "[knx][server][unit]")
    {
        detail::scripted_transport transport;
        transport.timeout_receives = 1u;
        generic_server server {transport};
        std::stop_source stop_source;
        bool cancelled {};
        completion::executor executor;
        executor.spawn(
            std::move(detail::serve_until_cancelled(server, stop_source, cancelled, executor)).with_stop_token(stop_source.get_token()));
        executor.run();
        CHECK(cancelled);
    }

    TEST_CASE("knx server send refreshes channel activity", "[knx][server][unit]")
    {
        detail::now_ms = 100u;
        detail::scripted_transport transport;
        generic_server server {
            transport,
            server_config {.max_channels = 1u, .inactivity_timeout_ms = 50u},
            {.clock_now = &detail::clock_now},
        };
        const connect_request_frame connect {
            .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
            .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
        };
        std::vector<std::uint8_t> packet(26u);
        REQUIRE(connection::encode_connect_request_packet(packet, connect).has_value());
        transport.incoming.push_back(packet);

        completion::executor connect_executor;
        std::uint8_t channel_id {};
        auto connect_run = [&]() -> task<void>
        {
            const auto result = co_await server.serve_once();
            channel_id = result ? result->channel_id : 0u;
            connect_executor.stop();
        };
        connect_executor.spawn(connect_run());
        connect_executor.run();
        REQUIRE(channel_id != 0u);

        detail::now_ms = 140u;
        completion::executor send_executor;
        bool sent {};
        auto send_run = [&]() -> task<void>
        {
            sent = (co_await server.send(channel_id, sample_cemi)).has_value();
            send_executor.stop();
        };
        send_executor.spawn(send_run());
        send_executor.run();
        REQUIRE(sent);

        detail::now_ms = 180u;
        REQUIRE(server.poll().has_value());
        CHECK(server.channel_active(channel_id));
        detail::now_ms = 0u;
    }

    TEST_CASE("knx server reset reopens a shut down instance", "[knx][server][unit]")
    {
        detail::scripted_transport transport;
        generic_server server {transport, server_config {.max_channels = 1u}};
        REQUIRE(server.shutdown().has_value());
        REQUIRE(server.reset().has_value());

        const connect_request_frame connect {
            .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
            .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
        };
        std::vector<std::uint8_t> packet(26u);
        REQUIRE(connection::encode_connect_request_packet(packet, connect).has_value());
        transport.incoming.push_back(packet);

        bool accepted {};
        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            accepted = (co_await server.serve_once()).has_value();
            executor.stop();
        };
        executor.spawn(run());
        executor.run();
        CHECK(accepted);
        CHECK(server.active_channels() == 1u);
    }

    TEST_CASE("knx server rejects unusable CONNECT HPAI before allocation", "[knx][server][unit]")
    {
        detail::scripted_transport transport;
        generic_server server {transport};
        const connect_request_frame valid {
            .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
            .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
        };
        std::vector<std::uint8_t> packet(26u);
        REQUIRE(connection::encode_connect_request_packet(packet, valid).has_value());
        packet[7u] = 0x02u;
        transport.incoming.push_back(packet);

        bool rejected {};
        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            const auto result = co_await server.serve_once();
            rejected = !result.has_value() && result.error() == make_error_code(error::unsupported_hpai);
            executor.stop();
        };
        executor.spawn(run());
        executor.run();

        CHECK(rejected);
        CHECK(server.active_channels() == 0u);
        REQUIRE(transport.sent_packets().size() == 1u);
        const auto response = connection::decode_connect_response_packet(transport.sent_packets().front());
        REQUIRE(response.has_value());
        CHECK(response->channel_id == 0u);
        CHECK(response->status == connect_status::host_protocol_type);
    }

    // A KNXnet/IP server that never answers SEARCH cannot be found by ETS or by any other client, however
    // well its tunnelling works. The answer goes to the discovery endpoint the request named, not back to
    // the multicast group the request arrived on.
    TEST_CASE("knx server answers a search request", "[knx][server][integration]")
    {
        detail::scripted_transport transport;
        const server_config config {
            .control_endpoint = hpai {ipv4_endpoint {{192u, 0u, 2u, 20u}, 3671u}, 0x01u},
            .device_info_blocks = {0x04u, 0x02u, 0x01u, 0x00u},
        };
        generic_server server {transport, config};

        std::array<std::uint8_t, frame::communication_header_size + discovery::search_request_body_size> request {};
        REQUIRE(discovery::encode_search_request_packet(request,
                                                        discovery::search_request_frame {
                                                            hpai {ipv4_endpoint {{192u, 0u, 2u, 99u}, 3672u}, 0x01u},
                                                        })
                    .has_value());
        transport.incoming.emplace_back(request.begin(), request.end());

        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            static_cast<void>(co_await server.serve_once());
            executor.stop();
        };
        executor.spawn(run());
        executor.run();

        REQUIRE(transport.sent_packets().size() == 1u);
        const auto answer = discovery::decode_search_response_packet(transport.sent_packets().front());
        REQUIRE(answer.has_value());
        CHECK(answer->control_endpoint.endpoint.address[3u] == 20u);
        CHECK(answer->device_info_blocks == byte_buffer_t {0x04u, 0x02u, 0x01u, 0x00u});

        // Sent to the discovery endpoint the request named, 192.0.2.99:3672.
        const auto& destination = reinterpret_cast<const sockaddr_in&>(transport.sent_peers().front());
        CHECK(destination.sin_addr.s_addr == htonl(0xC0000263u));
        CHECK(destination.sin_port == htons(3672u));
    }

    TEST_CASE("knx server answers a description request", "[knx][server][integration]")
    {
        detail::scripted_transport transport;
        const server_config config {
            .control_endpoint = hpai {ipv4_endpoint {{192u, 0u, 2u, 20u}, 3671u}, 0x01u},
            .device_info_blocks = {0x04u, 0x02u, 0x01u, 0x00u},
        };
        generic_server server {transport, config};

        std::array<std::uint8_t, frame::communication_header_size + discovery::description_request_body_size> request {};
        REQUIRE(discovery::encode_description_request_packet(request,
                                                             discovery::description_request_frame {
                                                                 hpai {ipv4_endpoint {{192u, 0u, 2u, 99u}, 3672u}, 0x01u},
                                                             })
                    .has_value());
        transport.incoming.emplace_back(request.begin(), request.end());

        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            static_cast<void>(co_await server.serve_once());
            executor.stop();
        };
        executor.spawn(run());
        executor.run();

        REQUIRE(transport.sent_packets().size() == 1u);
        const auto answer = discovery::decode_description_response_packet(transport.sent_packets().front());
        REQUIRE(answer.has_value());
        CHECK(answer->device_info_blocks == byte_buffer_t {0x04u, 0x02u, 0x01u, 0x00u});
    }

    TEST_CASE("knx server answers a route-back search at the datagram source", "[knx][server][integration]")
    {
        detail::scripted_transport transport;
        transport.receive_address = 0x0A000005u; // 10.0.0.5, the address NAT presents
        transport.receive_port = 51234u;
        const server_config config {
            .control_endpoint = hpai {ipv4_endpoint {{192u, 0u, 2u, 20u}, 3671u}, 0x01u},
            .device_info_blocks = {0x04u, 0x02u, 0x01u, 0x00u},
        };
        generic_server server {transport, config};

        // The all-zero HPAI a client behind NAT sends: it cannot know the address the server will see.
        std::array<std::uint8_t, frame::communication_header_size + discovery::search_request_body_size> request {};
        REQUIRE(
            discovery::encode_search_request_packet(request, discovery::search_request_frame {hpai {ipv4_endpoint {}, 0x01u}}).has_value());
        transport.incoming.emplace_back(request.begin(), request.end());

        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            static_cast<void>(co_await server.serve_once());
            executor.stop();
        };
        executor.spawn(run());
        executor.run();

        REQUIRE(transport.sent_peers().size() == 1u);
        const auto& destination = reinterpret_cast<const sockaddr_in&>(transport.sent_peers().front());
        CHECK(destination.sin_addr.s_addr == htonl(0x0A000005u));
        CHECK(destination.sin_port == htons(51234u));
    }

    TEST_CASE("knx server accepts a route-back connect request", "[knx][server][integration]")
    {
        detail::scripted_transport transport;
        transport.receive_address = 0x0A000005u;
        transport.receive_port = 51234u;
        generic_server server {transport, server_config {.max_channels = 1u}};

        std::array<std::uint8_t, frame::communication_header_size + connection::connect_request_body_size> request {};
        REQUIRE(connection::encode_connect_request_packet(
                    request, connect_request_frame {hpai {ipv4_endpoint {}, 0x01u}, hpai {ipv4_endpoint {}, 0x01u}})
                    .has_value());
        transport.incoming.emplace_back(request.begin(), request.end());

        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            static_cast<void>(co_await server.serve_once());
            executor.stop();
        };
        executor.spawn(run());
        executor.run();

        REQUIRE(transport.sent_packets().size() == 1u);
        const auto response = connection::decode_connect_response_packet(transport.sent_packets().front());
        REQUIRE(response.has_value());
        CHECK(response->status == connect_status::no_error);
        CHECK(server.active_channels() == 1u);
    }

    TEST_CASE("knx server refuses an unsupported tunnelling layer", "[knx][server][integration]")
    {
        detail::scripted_transport transport;
        generic_server server {transport};
        std::array<std::uint8_t, frame::communication_header_size + connection::connect_request_body_size> request {};
        // A raw tunnel is a layer the codec carries and this server does not serve, so the request decodes and
        // is refused with E_TUNNELLING_LAYER rather than dropped as malformed.
        REQUIRE(connection::encode_connect_request_packet(request,
                                                          connect_request_frame {
                                                              hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3671u}, 0x01u},
                                                              hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3672u}, 0x01u},
                                                              connection::tunnel_raw_layer,
                                                          })
                    .has_value());
        transport.incoming.emplace_back(request.begin(), request.end());

        std::size_t refused {};
        completion::executor executor;
        executor.spawn(detail::serve_counting(server, {1u, make_error_code(error::unsupported_connection_type)}, refused, executor));
        executor.run();

        CHECK(refused == 1u);
        REQUIRE(transport.sent_packets().size() == 1u);
        const auto response = connection::decode_connect_response_packet(transport.sent_packets().front());
        REQUIRE(response.has_value());
        CHECK(response->channel_id == 0u);
        CHECK(response->status == connect_status::tunnelling_layer);
        CHECK(server.active_channels() == 0u);
    }

    namespace detail
    {
        /// @brief Drives one serve_once and returns what the server sent.
        inline void serve_one(detail::scripted_transport& /* transport */, generic_server& server)
        {
            completion::executor executor;
            auto run = [&]() -> task<void>
            {
                static_cast<void>(co_await server.serve_once());
                executor.stop();
            };
            executor.spawn(run());
            executor.run();
        }

        /// @brief Builds a SEARCH_REQUEST_EXTENDED with one parameter block.
        [[nodiscard]] inline std::vector<std::uint8_t> extended_search(const discovery::search_parameter& parameter)
        {
            const discovery::extended_search_request_frame request {
                hpai {ipv4_endpoint {{192u, 0u, 2u, 99u}, 3672u}, 0x01u},
                {parameter},
            };
            const auto size = discovery::extended_search_request_size(request);
            REQUIRE(size.has_value());
            std::vector<std::uint8_t> packet(*size, 0u);
            REQUIRE(discovery::encode_extended_search_request_packet(packet, request).has_value());
            return packet;
        }
    }

    TEST_CASE("knx server answers an extended search it matches", "[knx][server][integration]")
    {
        detail::scripted_transport transport;
        const server_config config {
            .control_endpoint = hpai {ipv4_endpoint {{192u, 0u, 2u, 20u}, 3671u}, 0x01u},
            .device_info_blocks = {0x04u, 0x02u, 0x01u, 0x00u},
            .mac_address = {0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u},
        };
        generic_server server {transport, config};

        transport.incoming.emplace_back(detail::extended_search(
            discovery::search_parameter {true, discovery::search_parameter_type::mac_address, {0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u}}));
        detail::serve_one(transport, server);

        REQUIRE(transport.sent_packets().size() == 1u);
        const auto answer = discovery::decode_extended_search_response_packet(transport.sent_packets().front());
        REQUIRE(answer.has_value());
        CHECK(answer->control_endpoint.endpoint.address[3u] == 20u);
    }

    TEST_CASE("knx server limits an extended response to requested DIBs", "[knx][server][integration]")
    {
        detail::scripted_transport transport;
        dib::device_info info {};
        info.address = individual_address {1u, 1u, 0u};
        const server_config config {
            .control_endpoint = hpai {ipv4_endpoint {{192u, 0u, 2u, 20u}, 3671u}, 0x01u},
            .description_blocks =
                {
                    dib::block {info},
                    dib::block {
                        dib::supported_service_families {false, {{dib::service_family::core, 2u}, {dib::service_family::tunnelling, 2u}}}},
                },
        };
        generic_server server {transport, config};
        transport.incoming.emplace_back(detail::extended_search(discovery::search_parameter {
            true, discovery::search_parameter_type::request_dibs, {static_cast<std::uint8_t>(dib::block_type::device_info)}}));
        detail::serve_one(transport, server);

        REQUIRE(transport.sent_packets().size() == 1u);
        const auto response = discovery::decode_extended_search_response_packet(transport.sent_packets().front());
        REQUIRE(response.has_value());
        const auto blocks = dib::decode_all(response->device_info_blocks);
        REQUIRE(blocks.has_value());
        REQUIRE(blocks->size() == 1u);
        CHECK(std::holds_alternative<dib::device_info>(blocks->front()));
    }

    // The point of the mandatory flag: a searcher narrowing by MAC wants only the server it named, not an
    // answer from every other one explaining that it does not match.
    TEST_CASE("knx server stays silent on a mandatory parameter it cannot match", "[knx][server][integration]")
    {
        detail::scripted_transport transport;
        const server_config config {
            .control_endpoint = hpai {ipv4_endpoint {{192u, 0u, 2u, 20u}, 3671u}, 0x01u},
            .device_info_blocks = {0x04u, 0x02u, 0x01u, 0x00u},
            .mac_address = {0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u},
        };
        generic_server server {transport, config};

        transport.incoming.emplace_back(detail::extended_search(
            discovery::search_parameter {true, discovery::search_parameter_type::mac_address, {0xAAu, 0xBBu, 0xCCu, 0xDDu, 0xEEu, 0xFFu}}));
        detail::serve_one(transport, server);
        CHECK(transport.sent_packets().empty());

        // Programming mode, which this server is not in.
        transport.incoming.emplace_back(
            detail::extended_search(discovery::search_parameter {true, discovery::search_parameter_type::programming_mode, {}}));
        detail::serve_one(transport, server);
        CHECK(transport.sent_packets().empty());
    }

    TEST_CASE("knx server answers when an unmatched parameter is optional", "[knx][server][integration]")
    {
        detail::scripted_transport transport;
        const server_config config {
            .control_endpoint = hpai {ipv4_endpoint {{192u, 0u, 2u, 20u}, 3671u}, 0x01u},
            .device_info_blocks = {0x04u, 0x02u, 0x01u, 0x00u},
        };
        generic_server server {transport, config};

        transport.incoming.emplace_back(
            detail::extended_search(discovery::search_parameter {false, discovery::search_parameter_type::programming_mode, {}}));
        detail::serve_one(transport, server);
        CHECK(transport.sent_packets().size() == 1u);
    }

    namespace detail
    {
        /// @brief The description a server that serves core and tunnelling advertises.
        [[nodiscard]] inline std::vector<dib::block> serving_core_and_tunnelling()
        {
            dib::device_info info {};
            info.address = individual_address {1u, 1u, 0u};
            info.set_name("kmx test server");
            return {
                dib::block {info},
                dib::block {
                    dib::supported_service_families {false, {{dib::service_family::core, 2u}, {dib::service_family::tunnelling, 2u}}}},
            };
        }

        /// @brief Builds a SEARCH_REQUEST_EXTENDED selecting by one service family and version.
        [[nodiscard]] inline std::vector<std::uint8_t> select_by_service(const dib::service_family family, const std::uint8_t version,
                                                                         const bool mandatory = true)
        {
            return extended_search(discovery::search_parameter {
                mandatory,
                discovery::search_parameter_type::service,
                {static_cast<std::uint8_t>(family), version},
            });
        }
    }

    // The point of modelling service families rather than shipping opaque description octets: the server
    // answers a select-by-service search from the very list it advertises, so a client cannot be told one
    // thing by a search and another by a description of the same server.
    TEST_CASE("knx server answers a search for a family it serves", "[knx][server][integration]")
    {
        detail::scripted_transport transport;
        const server_config config {
            .control_endpoint = hpai {ipv4_endpoint {{192u, 0u, 2u, 20u}, 3671u}, 0x01u},
            .description_blocks = detail::serving_core_and_tunnelling(),
        };
        generic_server server {transport, config};

        transport.incoming.emplace_back(detail::select_by_service(dib::service_family::tunnelling, 2u));
        detail::serve_one(transport, server);

        REQUIRE(transport.sent_packets().size() == 1u);
        const auto answer = discovery::decode_extended_search_response_packet(transport.sent_packets().front());
        REQUIRE(answer.has_value());

        // And the description it answered with really does name the family that was asked for.
        const auto blocks = dib::decode_all(answer->device_info_blocks);
        REQUIRE(blocks.has_value());
        const auto* families = dib::find_service_families(*blocks);
        REQUIRE(families != nullptr);
        CHECK(families->contains(dib::service_family::tunnelling, 2u));
        REQUIRE(dib::find_device_info(*blocks) != nullptr);
        CHECK(dib::find_device_info(*blocks)->name() == "kmx test server");
    }

    TEST_CASE("knx server declines a search for a family it does not serve", "[knx][server][integration]")
    {
        detail::scripted_transport transport;
        const server_config config {
            .control_endpoint = hpai {ipv4_endpoint {{192u, 0u, 2u, 20u}, 3671u}, 0x01u},
            .description_blocks = detail::serving_core_and_tunnelling(),
        };
        generic_server server {transport, config};

        // A family it does not serve at all.
        transport.incoming.emplace_back(detail::select_by_service(dib::service_family::routing, 1u));
        detail::serve_one(transport, server);
        CHECK(transport.sent_packets().empty());

        // And a version of a family it serves, but at a lower version than asked for.
        transport.incoming.emplace_back(detail::select_by_service(dib::service_family::tunnelling, 3u));
        detail::serve_one(transport, server);
        CHECK(transport.sent_packets().empty());
    }

    TEST_CASE("knx server still answers an optional service parameter", "[knx][server][integration]")
    {
        detail::scripted_transport transport;
        const server_config config {
            .control_endpoint = hpai {ipv4_endpoint {{192u, 0u, 2u, 20u}, 3671u}, 0x01u},
            .description_blocks = detail::serving_core_and_tunnelling(),
        };
        generic_server server {transport, config};

        transport.incoming.emplace_back(detail::select_by_service(dib::service_family::routing, 1u, false));
        detail::serve_one(transport, server);
        CHECK(transport.sent_packets().size() == 1u);
    }

    TEST_CASE("knx server refuses a malformed verbatim description", "[knx][server][unit]")
    {
        detail::scripted_transport transport;
        const server_config config {
            .control_endpoint = hpai {ipv4_endpoint {{192u, 0u, 2u, 20u}, 3671u}, 0x01u},
            // A block claiming more octets than it has: caught here rather than sent to a peer.
            .device_info_blocks = {0x08u, 0x02u, 0x04u},
        };
        generic_server server {transport, config};

        std::array<std::uint8_t, frame::communication_header_size + discovery::search_request_body_size> request {};
        REQUIRE(discovery::encode_search_request_packet(request,
                                                        discovery::search_request_frame {
                                                            hpai {ipv4_endpoint {{192u, 0u, 2u, 99u}, 3672u}, 0x01u},
                                                        })
                    .has_value());
        transport.incoming.emplace_back(request.begin(), request.end());
        detail::serve_one(transport, server);
        CHECK(transport.sent_packets().empty());
    }
}
