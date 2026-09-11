/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/knx/gateway.hpp>
#include <kmx/aio/knx/routing.hpp>
#include <kmx/aio/test/knx/telegram.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <netinet/in.h>
#include <vector>

namespace kmx::aio::test::knx::routing_client_test
{
    using namespace kmx::aio::knx;
    std::uint32_t routing_now_ms {};

    [[nodiscard]] std::uint32_t routing_clock_now() noexcept
    {
        return routing_now_ms;
    }

    class loopback_routing_transport final: public datagram_transport
    {
    public:
        bool joined {};
        bool invalid_peer {};
        std::vector<std::uint8_t> last_sent {};

        void enqueue(std::vector<std::uint8_t> packet)
        {
            incoming_.push_back(std::move(packet));
        }

        [[nodiscard]] task_returning_expected_size_t send(
            const cspan_byte_t payload,
            const sockaddr*,
            const ::socklen_t) noexcept(false) override
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
            last_sent.assign(bytes, bytes + payload.size());
            co_return expected_size_t {payload.size()};
        }

        [[nodiscard]] task_returning_expected_size_t receive(
            const span_byte_t buffer,
            transport_peer& peer) noexcept(false) override
        {
            if (incoming_.empty())
                co_return std::unexpected(make_error_code(error::timeout));

            const auto payload = std::move(incoming_.front());
            incoming_.pop_front();
            if (payload.size() > buffer.size())
                co_return std::unexpected(make_error_code(error::invalid_length));

            peer = {};
            auto& sender = reinterpret_cast<sockaddr_in&>(peer.address);
            sender.sin_family = AF_INET;
            sender.sin_port = htons(3671u);
            sender.sin_addr.s_addr = htonl(0x7F000001u);
            peer.length = sizeof(sockaddr_in);
            if (invalid_peer)
            {
                peer.length = sizeof(sockaddr_in) - 1u;
                sender.sin_port = 0u;
            }

            for (std::size_t i = 0u; i < payload.size(); ++i)
                buffer[i] = static_cast<std::byte>(payload[i]);
            co_return expected_size_t {payload.size()};
        }

        [[nodiscard]] expected_void_t join_multicast_group(
            const multicast_group_configuration&) noexcept override
        {
            joined = true;
            return {};
        }

        [[nodiscard]] expected_void_t leave_multicast_group(
            const multicast_group_configuration&) noexcept override
        {
            joined = false;
            return {};
        }

    private:
        std::deque<std::vector<std::uint8_t>> incoming_ {};
    };

    namespace detail
    {
        /// @brief Reads one indication and records whether it carried the expected cEMI.
        task<void> receive_indication(routing::client& client, bool& received, completion::executor& executor) noexcept(false)
        {
            const auto indication = co_await client.receive_indication();
            received = indication.has_value() &&
                       (indication->cemi_bytes == std::vector<std::uint8_t>(sample_cemi.begin(), sample_cemi.end()));
            executor.stop();
        }

        /// @brief Reads one event and records whether it was the expected ROUTING_BUSY.
        task<void> receive_busy(routing::client& client, bool& busy_received, completion::executor& busy_executor) noexcept(false)
        {
            const auto result = co_await client.receive_event();
            const auto* value = result.has_value() ? std::get_if<routing::busy>(&result.value()) : nullptr;
            busy_received = (value != nullptr) && (value->wait_time_ms == 125u);
            busy_executor.stop();
        }

        /// @brief Reads one event and records whether it was the expected ROUTING_LOST_MESSAGE.
        task<void> receive_lost_message(routing::client& client, bool& lost_received, completion::executor& lost_executor) noexcept(false)
        {
            const auto result = co_await client.receive_event();
            const auto* value = result.has_value() ? std::get_if<routing::lost_message>(&result.value()) : nullptr;
            lost_received = (value != nullptr) && (value->count == 3u);
            lost_executor.stop();
        }

        /// @brief Reads one event past a reflected indication and records whether it was the expected busy.
        task<void> receive_busy_after_reflection(routing::client& client, bool& received_busy,
                                                 completion::executor& receive_executor) noexcept(false)
        {
            const auto result = co_await client.receive_event();
            const auto* value = result.has_value() ? std::get_if<routing::busy>(&result.value()) : nullptr;
            received_busy = (value != nullptr) && (value->wait_time_ms == 75u);
            receive_executor.stop();
        }

        /// @brief Sends a busy and a lost-message control, decoding each one back off the transport.
        task<void> send_busy_and_lost(routing::client& client, const loopback_routing_transport& transport, bool& sent_busy,
                                      bool& sent_lost, completion::executor& executor) noexcept(false)
        {
            sent_busy = (co_await client.send_busy(routing::busy {.wait_time_ms = 90u})).has_value();
            const auto busy = routing::decode_busy_packet(transport.last_sent);
            sent_busy = sent_busy && busy.has_value() && (busy->wait_time_ms == 90u);
            sent_lost = (co_await client.send_lost_message(routing::lost_message {.count = 2u})).has_value();
            const auto lost = routing::decode_lost_message_packet(transport.last_sent);
            sent_lost = sent_lost && lost.has_value() && (lost->count == 2u);
            executor.stop();
        }
    } // namespace detail

    // Golden wire vectors, the routing counterpart of the compile-time cEMI vectors. Encoder and decoder
    // agree with each other by construction, so only bytes captured from the specification can catch the
    // whole codec drifting - which is how a connection header that ROUTING_INDICATION does not have, and
    // two-octet bodies for services that define four and six, survived a green round-trip suite.
    //
    // KNX System Specifications, 03/08/05 "KNXnet/IP Routing".
    TEST_CASE("knx routing indication is a bare cEMI frame after the header", "[knx][routing][unit]")
    {
        // 06 10 | 05 30 | 00 11, then the cEMI frame itself - no channel id, no sequence, no reserved.
        const std::array<std::uint8_t, frame::communication_header_size + sample_cemi_size> expected {
            0x06u, 0x10u, 0x05u, 0x30u, 0x00u, 0x11u,
            0x11u, 0x00u, 0xBCu, 0xE0u, 0x11u, 0x01u, 0x0Au, 0x03u, 0x01u, 0x00u, 0x81u,
        };

        std::array<std::uint8_t, expected.size()> encoded {};
        REQUIRE(routing::encode_indication_packet(encoded, routing::indication {sample_cemi}).has_value());
        CHECK(encoded == expected);

        const auto decoded = routing::decode_indication_packet(expected);
        REQUIRE(decoded.has_value());
        CHECK(decoded->cemi_bytes.size() == sample_cemi_size);
        CHECK(std::equal(decoded->cemi_bytes.begin(), decoded->cemi_bytes.end(), sample_cemi.begin()));
    }

    TEST_CASE("knx routing lost message carries a four-octet information block", "[knx][routing][unit]")
    {
        // 06 10 | 05 31 | 00 0A, then structure length 04, device state 21, lost count 0005.
        const std::array<std::uint8_t, frame::communication_header_size + routing::lost_message_body_size> expected {
            0x06u, 0x10u, 0x05u, 0x31u, 0x00u, 0x0Au, 0x04u, 0x21u, 0x00u, 0x05u,
        };

        std::array<std::uint8_t, expected.size()> encoded {};
        REQUIRE(routing::encode_lost_message_packet(encoded, routing::lost_message {.device_state = 0x21u, .count = 5u}).has_value());
        CHECK(encoded == expected);

        const auto decoded = routing::decode_lost_message_packet(expected);
        REQUIRE(decoded.has_value());
        CHECK(decoded->device_state == 0x21u);
        CHECK(decoded->count == 5u);
    }

    TEST_CASE("knx routing busy carries a six-octet information block", "[knx][routing][unit]")
    {
        // 06 10 | 05 32 | 00 0C, then structure length 06, device state 21, wait 0064, control 0000.
        const std::array<std::uint8_t, frame::communication_header_size + routing::busy_body_size> expected {
            0x06u, 0x10u, 0x05u, 0x32u, 0x00u, 0x0Cu, 0x06u, 0x21u, 0x00u, 0x64u, 0x00u, 0x00u,
        };

        std::array<std::uint8_t, expected.size()> encoded {};
        REQUIRE(routing::encode_busy_packet(encoded, routing::busy {.device_state = 0x21u, .wait_time_ms = 100u}).has_value());
        CHECK(encoded == expected);

        const auto decoded = routing::decode_busy_packet(expected);
        REQUIRE(decoded.has_value());
        CHECK(decoded->device_state == 0x21u);
        CHECK(decoded->wait_time_ms == 100u);
        CHECK(decoded->control_field == 0u);
    }

    TEST_CASE("knx routing control decoders reject a mismatched structure length", "[knx][routing][unit]")
    {
        std::array<std::uint8_t, frame::communication_header_size + routing::busy_body_size> busy_packet {};
        REQUIRE(routing::encode_busy_packet(busy_packet, routing::busy {.wait_time_ms = 100u}).has_value());
        busy_packet[frame::communication_header_size] = 0x04u; // the lost-message block size, not this one
        const auto busy = routing::decode_busy_packet(busy_packet);
        REQUIRE(!busy.has_value());
        CHECK(busy.error() == make_error_code(error::malformed_frame));

        std::array<std::uint8_t, frame::communication_header_size + routing::lost_message_body_size> lost_packet {};
        REQUIRE(routing::encode_lost_message_packet(lost_packet, routing::lost_message {.count = 1u}).has_value());
        lost_packet[frame::communication_header_size] = 0x06u;
        const auto lost = routing::decode_lost_message_packet(lost_packet);
        REQUIRE(!lost.has_value());
        CHECK(lost.error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx routing client joins and leaves multicast runtime", "[knx][routing][unit]")
    {
        loopback_routing_transport transport;
        routing::client client {transport};
        REQUIRE(client.start().has_value());
        CHECK(transport.joined);
        REQUIRE(client.stop().has_value());
        CHECK(!transport.joined);
    }

    TEST_CASE("knx routing client records busy, lost and reflected messages", "[knx][routing][unit]")
    {
        loopback_routing_transport transport;
        routing::client client {transport};
        client.note_busy(250u);
        client.note_lost();
        client.note_reflected();

        CHECK(client.counters().busy_messages == 1u);
        CHECK(client.counters().lost_messages == 1u);
        CHECK(client.counters().reflected_messages == 1u);
        CHECK(client.counters().busy_backoff_ms == 250u);
    }

    TEST_CASE("knx gateway stops routing and server lifecycles together", "[knx][gateway][unit]")
    {
        loopback_routing_transport transport;
        gateway value {transport};
        REQUIRE(value.start().has_value());
        CHECK(transport.joined);
        REQUIRE(value.stop().has_value());
        CHECK(!transport.joined);

        completion::executor executor;
        bool shut_down {};
        auto run = [&]() -> task<void>
        {
            const auto result = co_await value.serve_once();
            shut_down = !result.has_value() && result.error() == make_error_code(error::shutdown);
            executor.stop();
        };
        executor.spawn(run());
        executor.run();
        CHECK(shut_down);
    }

    TEST_CASE("knx gateway exposes continuous server loop", "[knx][gateway][unit]")
    {
        loopback_routing_transport transport;
        gateway value {transport};
        REQUIRE(value.shutdown().has_value());

        completion::executor executor;
        bool stopped {};
        auto run = [&]() -> task<void>
        {
            const auto result = co_await value.serve();
            stopped = !result.has_value() && result.error() == make_error_code(error::shutdown);
            executor.stop();
        };
        executor.spawn(run());
        executor.run();
        CHECK(stopped);
    }

    TEST_CASE("knx gateway can restart after stop", "[knx][gateway][unit]")
    {
        loopback_routing_transport transport;
        gateway value {transport};
        REQUIRE(value.start().has_value());
        REQUIRE(value.stop().has_value());
        REQUIRE(value.start().has_value());

        CHECK(transport.joined);
        REQUIRE(value.stop().has_value());
        CHECK(!transport.joined);
    }

    TEST_CASE("knx gateway reports which of its halves are secure", "[knx][gateway][unit]")
    {
        loopback_routing_transport transport;
        const gateway plain {transport};
        CHECK(!plain.server_secured());
        CHECK(!plain.router_secured());

        // Keys are checked when a half starts, not when it is built, so empty ones do here.
        static constexpr secure::serial_number_t serial {0x00u, 0xFAu, 0x12u, 0x34u, 0x56u, 0x78u};
        auto server_security = std::make_shared<secure::server_configuration>();
        server_security->serial_number = serial;
        secure::routing_configuration routing_security {};
        routing_security.serial_number = serial;
        const gateway secured {transport, server_config {.secure = std::move(server_security)}, routing::multicast_configuration {},
                               std::move(routing_security)};
        CHECK(secured.server_secured());
        CHECK(secured.router_secured());
    }

    TEST_CASE("knx routing client sends indication after start", "[knx][routing][integration]")
    {
        loopback_routing_transport transport;
        routing::client client {transport};
        REQUIRE(client.start().has_value());

        bool sent {};
        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            sent = (co_await client.send_indication(routing::indication {sample_cemi})).has_value();
            executor.stop();
        };

        executor.spawn(run());
        executor.run();

        REQUIRE(sent);
        const auto decoded = routing::decode_indication_packet(transport.last_sent);
        REQUIRE(decoded.has_value());
        CHECK(decoded->cemi_bytes.size() == sample_cemi.size());
    }

    TEST_CASE("knx routing client receives indication payload", "[knx][routing][integration]")
    {
        loopback_routing_transport transport;
        routing::client client {transport};
        REQUIRE(client.start().has_value());

        std::array<std::uint8_t, frame::communication_header_size + sample_cemi.size()> packet {};
        REQUIRE(routing::encode_indication_packet(packet, routing::indication {sample_cemi}).has_value());
        transport.enqueue(std::vector<std::uint8_t>(packet.begin(), packet.end()));

        bool received {};
        completion::executor executor;

        executor.spawn(detail::receive_indication(client, received, executor));
        executor.run();
        CHECK(received);
    }

    TEST_CASE("knx routing client receives busy and lost controls", "[knx][routing][integration]")
    {
        loopback_routing_transport transport;
        routing::client client {transport};
        REQUIRE(client.start().has_value());

        std::array<std::uint8_t, frame::communication_header_size + routing::busy_body_size> busy_packet {};
        REQUIRE(routing::encode_busy_packet(busy_packet, routing::busy {.wait_time_ms = 125u}).has_value());
        transport.enqueue(std::vector<std::uint8_t>(busy_packet.begin(), busy_packet.end()));

        bool busy_received {};
        completion::executor busy_executor;
        busy_executor.spawn(detail::receive_busy(client, busy_received, busy_executor));
        busy_executor.run();
        CHECK(busy_received);
        CHECK(client.counters().busy_messages == 1u);

        std::array<std::uint8_t, frame::communication_header_size + routing::lost_message_body_size> lost_packet {};
        REQUIRE(routing::encode_lost_message_packet(lost_packet, routing::lost_message {.count = 3u}).has_value());
        transport.enqueue(std::vector<std::uint8_t>(lost_packet.begin(), lost_packet.end()));

        bool lost_received {};
        completion::executor lost_executor;
        lost_executor.spawn(detail::receive_lost_message(client, lost_received, lost_executor));
        lost_executor.run();
        CHECK(lost_received);
        CHECK(client.counters().lost_messages == 3u);
    }

    TEST_CASE("knx routing client rejects malformed source peer metadata", "[knx][routing][unit]")
    {
        loopback_routing_transport transport;
        transport.invalid_peer = true;
        routing::client client {transport};
        REQUIRE(client.start().has_value());
        std::array<std::uint8_t, frame::communication_header_size + routing::busy_body_size> packet {};
        REQUIRE(routing::encode_busy_packet(packet, routing::busy {.wait_time_ms = 10u}).has_value());
        transport.enqueue(std::vector<std::uint8_t>(packet.begin(), packet.end()));

        bool rejected {};
        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            const auto result = co_await client.receive_event();
            rejected = !result.has_value() && result.error() == make_error_code(error::connection_failed);
            executor.stop();
        };
        executor.spawn(run());
        executor.run();
        CHECK(rejected);
    }

    TEST_CASE("knx routing client suppresses reflected indications", "[knx][routing][integration]")
    {
        loopback_routing_transport transport;
        routing::client client {transport};
        REQUIRE(client.start().has_value());

        completion::executor send_executor;
        auto send_run = [&]() -> task<void>
        {
            REQUIRE((co_await client.send_indication(routing::indication {sample_cemi})).has_value());
            send_executor.stop();
        };
        send_executor.spawn(send_run());
        send_executor.run();

        transport.enqueue(transport.last_sent);
        std::array<std::uint8_t, frame::communication_header_size + routing::busy_body_size> busy_packet {};
        REQUIRE(routing::encode_busy_packet(busy_packet, routing::busy {.wait_time_ms = 75u}).has_value());
        transport.enqueue(std::vector<std::uint8_t>(busy_packet.begin(), busy_packet.end()));

        bool received_busy {};
        completion::executor receive_executor;
        receive_executor.spawn(detail::receive_busy_after_reflection(client, received_busy, receive_executor));
        receive_executor.run();
        CHECK(received_busy);
        CHECK(client.counters().reflected_messages == 1u);
    }

    TEST_CASE("knx routing client suppresses reflections from recent sends", "[knx][routing][integration]")
    {
        loopback_routing_transport transport;
        routing::client client {transport};
        REQUIRE(client.start().has_value());

        std::vector<std::uint8_t> first_packet {};
        completion::executor send_executor;
        auto send_run = [&]() -> task<void>
        {
            REQUIRE((co_await client.send_indication(routing::indication {sample_cemi})).has_value());
            first_packet = transport.last_sent;
            REQUIRE((co_await client.send_busy(routing::busy {.wait_time_ms = 75u})).has_value());
            send_executor.stop();
        };
        send_executor.spawn(send_run());
        send_executor.run();

        transport.enqueue(std::move(first_packet));
        std::array<std::uint8_t, frame::communication_header_size + routing::lost_message_body_size> lost_packet {};
        REQUIRE(routing::encode_lost_message_packet(lost_packet, routing::lost_message {.count = 3u}).has_value());
        transport.enqueue(std::vector<std::uint8_t>(lost_packet.begin(), lost_packet.end()));

        bool received_lost {};
        completion::executor receive_executor;
        receive_executor.spawn(detail::receive_lost_message(client, received_lost, receive_executor));
        receive_executor.run();
        CHECK(received_lost);
        CHECK(client.counters().reflected_messages == 1u);
    }

    TEST_CASE("knx routing client enforces busy backoff before sending", "[knx][routing][unit]")
    {
        routing_now_ms = 100u;
        loopback_routing_transport transport;
        routing::client client {transport, {}, &routing_clock_now};
        REQUIRE(client.start().has_value());
        client.note_busy(50u);

        bool blocked {};
        completion::executor blocked_executor;
        auto blocked_run = [&]() -> task<void>
        {
            const auto result = co_await client.send_indication(routing::indication {sample_cemi});
            blocked = !result.has_value() && result.error() == make_error_code(error::timeout);
            blocked_executor.stop();
        };
        blocked_executor.spawn(blocked_run());
        blocked_executor.run();
        CHECK(blocked);

        routing_now_ms = 150u;
        bool sent {};
        completion::executor sent_executor;
        auto sent_run = [&]() -> task<void>
        {
            sent = (co_await client.send_indication(routing::indication {sample_cemi})).has_value();
            sent_executor.stop();
        };
        sent_executor.spawn(sent_run());
        sent_executor.run();
        CHECK(sent);
        routing_now_ms = 0u;
    }

    TEST_CASE("knx routing client sends busy and lost controls", "[knx][routing][integration]")
    {
        loopback_routing_transport transport;
        routing::client client {transport};
        REQUIRE(client.start().has_value());

        bool sent_busy {};
        bool sent_lost {};
        completion::executor executor;
        executor.spawn(detail::send_busy_and_lost(client, transport, sent_busy, sent_lost, executor));
        executor.run();
        CHECK(sent_busy);
        CHECK(sent_lost);
    }

    TEST_CASE("knx routing client clears transient state on restart", "[knx][routing][unit]")
    {
        routing_now_ms = 100u;
        loopback_routing_transport transport;
        routing::client client {transport, {}, &routing_clock_now};
        REQUIRE(client.start().has_value());
        client.note_busy(100u);
        REQUIRE(client.stop().has_value());
        REQUIRE(client.start().has_value());

        bool sent {};
        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            sent = (co_await client.send_indication(routing::indication {sample_cemi})).has_value();
            executor.stop();
        };
        executor.spawn(run());
        executor.run();
        CHECK(sent);
        routing_now_ms = 0u;
    }
}
