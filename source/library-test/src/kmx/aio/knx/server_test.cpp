/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/knx/server.hpp>
#include <kmx/aio/test/knx/telegram.hpp>

#include <deque>
#include <netinet/in.h>
#include <vector>

namespace kmx::aio::test::knx::server_test
{
    using namespace kmx::aio::knx;
    std::uint32_t server_now_ms = 0u;

    [[nodiscard]] std::uint32_t server_clock_now() noexcept
    {
        return server_now_ms;
    }

    class server_transport final: public datagram_transport
    {
    public:
        bool ipv6_peer = false;
        std::uint16_t receive_port = 40000u;
        std::uint32_t receive_address = 0x7F000001u;
        std::uint32_t timeout_receives = 0u;
        std::deque<std::vector<std::uint8_t>> incoming {};
        std::vector<std::vector<std::uint8_t>> outgoing {};
        std::vector<sockaddr_storage> outgoing_peers {};

        [[nodiscard]] task_returning_expected_size_t send(
            const cspan_byte_t payload, const sockaddr* peer, const ::socklen_t peer_length) noexcept(false) override
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
            outgoing.emplace_back(bytes, bytes + payload.size());
            sockaddr_storage destination {};
            if ((peer != nullptr) && (peer_length <= sizeof(destination)))
                std::memcpy(&destination, peer, peer_length);
            outgoing_peers.push_back(destination);
            co_return expected_size_t {payload.size()};
        }

        [[nodiscard]] task_returning_expected_size_t receive(
            const span_byte_t buffer, transport_peer& peer) noexcept(false) override
        {
            if (timeout_receives != 0u)
            {
                --timeout_receives;
                co_return std::unexpected(make_error_code(error::timeout));
            }
            if (incoming.empty())
                co_return std::unexpected(make_error_code(error::timeout));
            const auto packet = std::move(incoming.front());
            incoming.pop_front();
            if (packet.size() > buffer.size())
                co_return std::unexpected(make_error_code(error::invalid_length));

            peer = {};
            if (ipv6_peer)
            {
                auto& address = reinterpret_cast<sockaddr_in6&>(peer.address);
                address.sin6_family = AF_INET6;
                address.sin6_addr = in6addr_loopback;
                address.sin6_port = htons(receive_port);
                peer.length = sizeof(sockaddr_in6);
            }
            else
            {
                auto& address = reinterpret_cast<sockaddr_in&>(peer.address);
                address.sin_family = AF_INET;
                address.sin_addr.s_addr = htonl(receive_address);
                address.sin_port = htons(receive_port);
                peer.length = sizeof(sockaddr_in);
            }
            for (std::size_t i = 0u; i < packet.size(); ++i)
                buffer[i] = static_cast<std::byte>(packet[i]);
            co_return expected_size_t {packet.size()};
        }
    };

    TEST_CASE("knx server allocates channel and handles tunnelling lifecycle", "[knx][server][integration]")
    {
        server_transport transport;
        generic_server server {transport, server_config {.max_channels = 2u, .first_assigned_address = individual_address {1u, 1u, 10u}}};

        const connect_request_frame connect {
            .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
            .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 2u}, 40001u}, 0x01u},
        };
        std::vector<std::uint8_t> connect_packet(26u);
        REQUIRE(connection::encode_connect_request_packet(connect_packet, connect).has_value());
        transport.incoming.push_back(connect_packet);

        bool connected = false;
        std::uint8_t channel_id = 0u;
        completion::executor executor;
        auto connect_task = [&]() -> task<void>
        {
            const auto result = co_await server.serve_once();
            connected = result.has_value() && result->cemi_bytes.empty();
            channel_id = result ? result->channel_id : 0u;
            executor.stop();
        };
        executor.spawn(connect_task());
        executor.run();

        REQUIRE(connected);
        CHECK(channel_id != 0u);
        CHECK(server.channel_active(channel_id));
        REQUIRE(transport.outgoing.size() == 1u);
        const auto response = connection::decode_connect_response_packet(transport.outgoing.front());
        REQUIRE(response.has_value());
        CHECK(response->channel_id == channel_id);
        CHECK(response->assigned_address == individual_address {1u, 1u, 10u});

        transport.outgoing.clear();
        transport.receive_port = 40001u;
        transport.receive_address = 0x7F000002u;
        std::vector<std::uint8_t> request(6u + 4u + sample_cemi.size());
        REQUIRE(frame::encode_tunnelling_request_packet(request, channel_id, 0u, sample_cemi).has_value());
        transport.incoming.push_back(request);

        bool received = false;
        completion::executor tunnel_executor;
        auto tunnel_task = [&]() -> task<void>
        {
            const auto result = co_await server.serve_once();
            received = result.has_value() && result->channel_id == channel_id &&
                       result->cemi_bytes == std::vector<std::uint8_t>(sample_cemi.begin(), sample_cemi.end());
            tunnel_executor.stop();
        };
        tunnel_executor.spawn(tunnel_task());
        tunnel_executor.run();
        CHECK(received);
        REQUIRE(transport.outgoing.size() == 1u);
        CHECK(frame::decode_tunnelling_ack_packet(transport.outgoing.front()).has_value());
        REQUIRE(transport.outgoing_peers.size() >= 2u);
        CHECK(ntohs(reinterpret_cast<const sockaddr_in&>(transport.outgoing_peers[1u]).sin_port) == 40001u);
                CHECK(reinterpret_cast<const sockaddr_in&>(transport.outgoing_peers[1u]).sin_addr.s_addr ==
                            htonl(0x7F000002u));

        transport.outgoing.clear();
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
        REQUIRE(transport.outgoing.size() == 1u);
        CHECK(frame::decode_tunnelling_ack_packet(transport.outgoing.front()).has_value());

        std::vector<std::uint8_t> out_of_order(6u + 4u + sample_cemi.size());
        REQUIRE(frame::encode_tunnelling_request_packet(out_of_order, channel_id, 9u, sample_cemi).has_value());
        transport.incoming.push_back(out_of_order);
        bool rejected = false;
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
        server_transport transport;
        generic_server server {transport, server_config {.max_channels = 1u}};
        const connect_request_frame connect {
            .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
            .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
        };
        std::vector<std::uint8_t> packet(26u);
        REQUIRE(connection::encode_connect_request_packet(packet, connect).has_value());
        transport.incoming.push_back(packet);

        completion::executor first_executor;
        bool first_connected = false;
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
        bool exhausted = false;
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

    TEST_CASE("knx server accepts an IPv6 CONNECT request", "[knx][server][integration][ipv6]")
    {
        server_transport transport;
        transport.ipv6_peer = true;
        generic_server server {transport};
        const ipv6_connect_request_frame connect {
            .control_endpoint = ipv6_hpai {ipv6_endpoint {{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u}, 40000u}, 0x01u},
            .data_endpoint = ipv6_hpai {ipv6_endpoint {{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u}, 40001u}, 0x01u},
        };
        std::vector<std::uint8_t> packet(50u);
        REQUIRE(connection::encode_ipv6_connect_request_packet(packet, connect).has_value());
        transport.incoming.push_back(packet);

        bool accepted = false;
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
        REQUIRE(transport.outgoing.size() == 1u);
        const auto response = connection::decode_ipv6_connect_response_packet(transport.outgoing.front());
        REQUIRE(response.has_value());
        CHECK(response->channel_id != 0u);
    }

    TEST_CASE("knx server expires inactive channels", "[knx][server][unit]")
    {
        server_now_ms = 100u;
        server_transport transport;
        generic_server server {
            transport,
            server_config {.max_channels = 1u, .inactivity_timeout_ms = 50u},
            &server_clock_now,
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

        server_now_ms = 151u;
        REQUIRE(server.poll().has_value());
        CHECK(server.active_channels() == 0u);
        server_now_ms = 0u;
    }

    TEST_CASE("knx server polls stale channels before serving next packet", "[knx][server][integration]")
    {
        server_now_ms = 100u;
        server_transport transport;
        generic_server server {
            transport,
            server_config {.max_channels = 1u, .inactivity_timeout_ms = 50u},
            &server_clock_now,
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

        server_now_ms = 151u;
        transport.incoming.push_back(packet);
        completion::executor second_executor;
        bool accepted = false;
        auto second = [&]() -> task<void>
        {
            accepted = (co_await server.serve_once()).has_value();
            second_executor.stop();
        };
        second_executor.spawn(second());
        second_executor.run();
        CHECK(accepted);
        CHECK(server.active_channels() == 1u);
        server_now_ms = 0u;
    }

    TEST_CASE("knx server serve retries timeout and honors cancellation", "[knx][server][unit]")
    {
        server_transport transport;
        transport.timeout_receives = 1u;
        generic_server server {transport};
        std::stop_source stop_source;
        bool cancelled = false;
        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            stop_source.request_stop();
            const auto result = co_await server.serve();
            cancelled = !result.has_value() && result.error() == make_error_code(error::shutdown);
            executor.stop();
        };
        executor.spawn(std::move(run()).with_stop_token(stop_source.get_token()));
        executor.run();
        CHECK(cancelled);
    }

    TEST_CASE("knx server send refreshes channel activity", "[knx][server][unit]")
    {
        server_now_ms = 100u;
        server_transport transport;
        generic_server server {
            transport,
            server_config {.max_channels = 1u, .inactivity_timeout_ms = 50u},
            &server_clock_now,
        };
        const connect_request_frame connect {
            .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
            .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
        };
        std::vector<std::uint8_t> packet(26u);
        REQUIRE(connection::encode_connect_request_packet(packet, connect).has_value());
        transport.incoming.push_back(packet);

        completion::executor connect_executor;
        std::uint8_t channel_id = 0u;
        auto connect_run = [&]() -> task<void>
        {
            const auto result = co_await server.serve_once();
            channel_id = result ? result->channel_id : 0u;
            connect_executor.stop();
        };
        connect_executor.spawn(connect_run());
        connect_executor.run();
        REQUIRE(channel_id != 0u);

        server_now_ms = 140u;
        completion::executor send_executor;
        bool sent = false;
        auto send_run = [&]() -> task<void>
        {
            sent = (co_await server.send(channel_id, sample_cemi)).has_value();
            send_executor.stop();
        };
        send_executor.spawn(send_run());
        send_executor.run();
        REQUIRE(sent);

        server_now_ms = 180u;
        REQUIRE(server.poll().has_value());
        CHECK(server.channel_active(channel_id));
        server_now_ms = 0u;
    }

    TEST_CASE("knx server reset reopens a shut down instance", "[knx][server][unit]")
    {
        server_transport transport;
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

        bool accepted = false;
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
        server_transport transport;
        generic_server server {transport};
        const connect_request_frame valid {
            .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
            .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 40000u}, 0x01u},
        };
        std::vector<std::uint8_t> packet(26u);
        REQUIRE(connection::encode_connect_request_packet(packet, valid).has_value());
        packet[7u] = 0x02u;
        transport.incoming.push_back(packet);

        bool rejected = false;
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
        CHECK(transport.outgoing.empty());
    }
}
