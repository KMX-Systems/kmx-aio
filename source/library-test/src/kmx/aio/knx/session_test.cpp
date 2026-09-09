/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/frame.hpp>
#include <kmx/aio/knx/session.hpp>
#include <kmx/aio/test/knx/telegram.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

namespace kmx::aio::test::knx::session_test
{
    using namespace kmx::aio::knx;

    TEST_CASE("knx session tracks a successful ack", "[knx][session][unit]")
    {
        tunnelling_session session {{ .max_retries = 2u, .ack_timeout_ms = 1000u }};

        REQUIRE(session.begin_request(7u, 21u, 1'000u).has_value());
        REQUIRE(session.state() == session_state::waiting_ack);
        REQUIRE(session.expired(999u) == false);
        REQUIRE(session.on_ack(7u, 21u).has_value());
        CHECK(session.state() == session_state::connected);
        CHECK(session.has_pending_request() == false);
    }

    TEST_CASE("knx session retries until exhaustion", "[knx][session][unit]")
    {
        tunnelling_session session {{ .max_retries = 2u, .ack_timeout_ms = 1000u }};
        REQUIRE(session.begin_request(9u, 42u, 100u).has_value());

        REQUIRE(session.on_timeout().has_value());
        CHECK(session.retries() == 1u);
        CHECK(!session.expired(100u));
        CHECK(!session.expired(1'099u));
        CHECK(session.expired(1'100u));
        REQUIRE(session.on_timeout().has_value());
        CHECK(session.retries() == 2u);
        REQUIRE(!session.on_timeout().has_value());
        CHECK(session.state() == session_state::closed);
    }

    TEST_CASE("knx session handles deadline rollover", "[knx][session][unit]")
    {
        tunnelling_session session {{ .max_retries = 1u, .ack_timeout_ms = 32u }};
        REQUIRE(session.begin_request(3u, 5u, 0xFFFF'FFF0u).has_value());
        CHECK(!session.expired(0xFFFF'FFEFu));
        CHECK(session.expired(0xFFFF'FFF0u));
        CHECK(session.expired(0x0000'0000u));

        REQUIRE(session.on_timeout().has_value());
        CHECK(!session.expired(0x0000'000Fu));
        CHECK(session.expired(0x0000'0010u));
        REQUIRE(session.on_ack(3u, 5u).has_value());
    }

    TEST_CASE("knx session uses modular retry deadlines", "[knx][session][unit]")
    {
        tunnelling_session session {{ .max_retries = 1u, .ack_timeout_ms = 100u }};
        REQUIRE(session.begin_request(3u, 5u, 0xFFFF'FFF0u).has_value());
        REQUIRE(session.on_timeout().has_value());
        CHECK(!session.expired(0x0000'0053u));
        CHECK(session.expired(0x0000'0054u));
    }

    TEST_CASE("knx session rejects mismatched ack", "[knx][session][unit]")
    {
        tunnelling_session session {{ .max_retries = 2u, .ack_timeout_ms = 1000u }};
        REQUIRE(session.begin_request(11u, 33u, 500u).has_value());

        const auto result = session.on_ack(11u, 34u);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::sequence_error));
    }

    TEST_CASE("knx session detects expiration", "[knx][session][unit]")
    {
        tunnelling_session session {{ .max_retries = 1u, .ack_timeout_ms = 100u }};
        REQUIRE(session.begin_request(3u, 5u, 100u).has_value());
        CHECK(session.expired(100u));
    }

    TEST_CASE("knx session rejects a negative tunnelling ack", "[knx][session][unit]")
    {
        tunnelling_session session {{ .max_retries = 1u, .ack_timeout_ms = 100u }};
        REQUIRE(session.begin_request(3u, 5u, 100u).has_value());

        const tunnelling_ack_frame ack { .channel_id = 3u, .sequence_number = 5u, .status = 0x21u };
        const auto result = session.on_ack(ack);

        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::connection_failed));
        CHECK(session.has_pending_request());
        CHECK(session.state() == session_state::waiting_ack);
    }

    TEST_CASE("knx session consumes a complete tunnelling ack packet", "[knx][session][integration]")
    {
        tunnelling_session session {{ .max_retries = 1u, .ack_timeout_ms = 100u }};
        REQUIRE(session.begin_request(3u, 5u, 100u).has_value());

        const std::array<std::uint8_t, 10u> packet {
            0x06u, 0x10u, 0x04u, 0x21u, 0x00u, 0x0Au,
            0x04u, 0x03u, 0x05u, 0x00u,
        };

        REQUIRE(session.on_ack_packet(packet).has_value());
        CHECK(session.state() == session_state::connected);
        CHECK(!session.has_pending_request());
    }

    TEST_CASE("knx session leaves state unchanged for malformed ack packets", "[knx][session][integration]")
    {
        tunnelling_session session {{ .max_retries = 1u, .ack_timeout_ms = 100u }};
        REQUIRE(session.begin_request(3u, 5u, 100u).has_value());

        const std::array<std::uint8_t, 6u> packet { 0x06u, 0x10u, 0x04u, 0x21u, 0x00u, 0x0Au };
        const auto result = session.on_ack_packet(packet);

        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::malformed_frame));
        CHECK(session.state() == session_state::waiting_ack);
        CHECK(session.has_pending_request());
    }

    TEST_CASE("knx session shutdown rejects new requests until reset", "[knx][session][unit]")
    {
        tunnelling_session session;
        REQUIRE(session.begin_request(3u, 5u, 100u).has_value());

        session.shutdown();
        CHECK(session.state() == session_state::closed);
        CHECK(!session.has_pending_request());
        REQUIRE(!session.begin_request(3u, 6u, 200u).has_value());

        session.reset();
        REQUIRE(session.begin_request(3u, 6u, 200u).has_value());
    }

    TEST_CASE("knx session allocates and wraps request sequences", "[knx][session][unit]")
    {
        tunnelling_session session;

        CHECK(session.next_sequence() == 0u);
        const auto first = session.begin_request(3u, 100u);
        REQUIRE(first.has_value());
        CHECK(first.value() == 0u);
        CHECK(session.next_sequence() == 1u);
        REQUIRE(session.on_ack(3u, 0u).has_value());

        REQUIRE(session.begin_request(3u, 1u, 200u).has_value());
        REQUIRE(session.on_ack(3u, 1u).has_value());
        session.reset();
        REQUIRE(session.begin_request(3u, 255u, 300u).has_value());
        REQUIRE(session.on_ack(3u, 255u).has_value());
        CHECK(session.next_sequence() == 0u);
    }

    TEST_CASE("knx session reports bounded send and shutdown errors", "[knx][session][unit]")
    {
        tunnelling_session session;
        REQUIRE(session.begin_request(3u, 5u, 100u).has_value());
        const auto queued = session.begin_request(3u, 6u, 200u);
        REQUIRE(!queued.has_value());
        CHECK(queued.error() == make_error_code(error::send_queue_full));

        session.shutdown();
        const auto stopped = session.begin_request(3u, 7u, 300u);
        REQUIRE(!stopped.has_value());
        CHECK(stopped.error() == make_error_code(error::shutdown));
    }

    TEST_CASE("knx session rejects invalid channel and sequence fields", "[knx][session][unit]")
    {
        tunnelling_session session;
        const auto invalid_channel = session.begin_request(0u, 1u, 100u);
        REQUIRE(!invalid_channel.has_value());
        CHECK(invalid_channel.error() == make_error_code(error::sequence_error));

        const auto invalid_sequence = session.begin_request(1u, 256u, 100u);
        REQUIRE(!invalid_sequence.has_value());
        CHECK(invalid_sequence.error() == make_error_code(error::sequence_error));
        CHECK(!session.has_pending_request());
    }

    TEST_CASE("knx session reports duplicate ack as a sequence error", "[knx][session][unit]")
    {
        tunnelling_session session;
        REQUIRE(session.begin_request(1u, 1u, 100u).has_value());
        REQUIRE(session.on_ack(1u, 1u).has_value());

        const auto duplicate = session.on_ack(1u, 1u);
        REQUIRE(!duplicate.has_value());
        CHECK(duplicate.error() == make_error_code(error::sequence_error));
    }

    TEST_CASE("knx session prepares a request atomically", "[knx][session][integration]")
    {
        tunnelling_session session;
        const auto& cemi = sample_cemi;
        std::array<std::uint8_t, sample_tunnelling_packet_size> packet {};

        const auto sequence = session.prepare_request_packet(packet, 3u, cemi, 100u);
        REQUIRE(sequence.has_value());
        CHECK(sequence.value() == 0u);
        CHECK(session.expected_sequence() == 0u);
        CHECK(packet[0] == 0x06u);
        CHECK(packet[2] == 0x04u);
        CHECK(packet[3] == 0x20u);
        CHECK(packet[6] == 0x04u); // connection header structure length
        CHECK(packet[7] == 3u);    // channel id
        CHECK(packet[8] == 0u);    // sequence counter

        const auto decoded = frame::decode_tunnelling_request_packet(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->channel_id == 3u);
        CHECK(decoded->sequence_number == 0u);
    }

    TEST_CASE("knx session does not consume sequence on encoding failure", "[knx][session][unit]")
    {
        tunnelling_session session;
        const std::array<std::uint8_t, 7u> cemi {};
        std::array<std::uint8_t, sample_tunnelling_packet_size> packet {};

        const auto result = session.prepare_request_packet(packet, 3u, cemi, 100u);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::malformed_frame));
        CHECK(session.next_sequence() == 0u);
        CHECK(!session.has_pending_request());
    }

    TEST_CASE("knx session rebuilds an identical retry packet", "[knx][session][integration]")
    {
        tunnelling_session session {{ .max_retries = 2u, .ack_timeout_ms = 100u }};
        const auto& cemi = sample_cemi;
        std::array<std::uint8_t, sample_tunnelling_packet_size> original {};
        REQUIRE(session.prepare_request_packet(original, 3u, cemi, 100u).has_value());
        CHECK(session.active_request_packet().size() == original.size());
        CHECK(std::equal(session.active_request_packet().begin(),
                 session.active_request_packet().end(),
                 original.begin()));

        std::array<std::uint8_t, sample_tunnelling_packet_size> before_retry {};
        CHECK(!session.prepare_retry_packet(before_retry).has_value());
        REQUIRE(session.on_timeout().has_value());

        std::array<std::uint8_t, sample_tunnelling_packet_size> retry {};
        REQUIRE(session.prepare_retry_packet(retry).has_value());
        CHECK(retry == original);
        REQUIRE(session.on_ack(3u, 0u).has_value());
        CHECK(!session.prepare_retry_packet(retry).has_value());
        CHECK(session.active_request_packet().empty());
    }

    TEST_CASE("knx session rejects undersized retry buffers", "[knx][session][unit]")
    {
        tunnelling_session session;
        const auto& cemi = sample_cemi;
        std::array<std::uint8_t, sample_tunnelling_packet_size> request {};
        REQUIRE(session.prepare_request_packet(request, 3u, cemi, 100u).has_value());
        REQUIRE(session.on_timeout().has_value());

        std::array<std::uint8_t, 17u> retry {};
        const auto result = session.prepare_retry_packet(retry);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::invalid_length));
    }

    TEST_CASE("knx session closes after a matching disconnect response", "[knx][session][integration]")
    {
        tunnelling_session session;
        REQUIRE(session.begin_request(3u, 5u, 100u).has_value());
        REQUIRE(session.on_ack(3u, 5u).has_value());

        const std::array<std::uint8_t, 8u> packet { 0x06u, 0x10u, 0x02u, 0x0Au,
                                                    0x00u, 0x08u, 0x03u, 0x00u };
            std::array<std::uint8_t, 8u> disconnect_request {};
            REQUIRE(session.prepare_disconnect_request_packet(disconnect_request).has_value());
        REQUIRE(session.on_disconnect_response_packet(packet).has_value());
        CHECK(session.state() == session_state::closed);
        CHECK(!session.has_pending_request());
    }

    TEST_CASE("knx session preserves state for an invalid disconnect response", "[knx][session][unit]")
    {
        tunnelling_session session;
        REQUIRE(session.begin_request(3u, 5u, 100u).has_value());
        REQUIRE(session.on_ack(3u, 5u).has_value());

        const std::array<std::uint8_t, 8u> packet { 0x06u, 0x10u, 0x02u, 0x0Au,
                                                    0x00u, 0x08u, 0x04u, 0x00u };
        std::array<std::uint8_t, 8u> disconnect_request {};
        REQUIRE(session.prepare_disconnect_request_packet(disconnect_request).has_value());
        const auto result = session.on_disconnect_response_packet(packet);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::sequence_error));
        CHECK(session.state() == session_state::closing);
    }

    TEST_CASE("knx session rejects disconnect response before closing", "[knx][session][unit]")
    {
        tunnelling_session session;
        const std::array<std::uint8_t, 8u> packet {
            0x06u, 0x10u, 0x02u, 0x0Au, 0x00u, 0x08u, 0x03u, 0x00u,
        };

        const auto result = session.on_disconnect_response_packet(packet);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::invalid_configuration));
        CHECK(session.state() == session_state::idle);
    }

    TEST_CASE("knx session consumes dispatched datagrams", "[knx][session][integration]")
    {
        tunnelling_session session;
        REQUIRE(session.begin_request(3u, 5u, 100u).has_value());

        std::array<std::uint8_t, 10u> packet {};
        REQUIRE(frame::encode_tunnelling_ack_packet(packet, 3u, 5u).has_value());
        REQUIRE(session.on_datagram(packet).has_value());
        CHECK(session.state() == session_state::connected);
    }

    TEST_CASE("knx session dispatches a raw matching tunnelling ack", "[knx][session][integration]")
    {
        tunnelling_session session;
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        REQUIRE(session.begin_request(3u, 5u, 100u).has_value());

        std::array<std::uint8_t, 10u> packet {};
        REQUIRE(frame::encode_tunnelling_ack_packet(packet, 3u, 5u).has_value());
        REQUIRE(session.dispatch_session_datagram(packet, 700u).has_value());
        CHECK(session.state() == session_state::connected);
        CHECK(!session.has_pending_request());
        CHECK(session.last_activity_ms() == 700u);
    }

    TEST_CASE("knx session preserves pending request on mismatched raw ack", "[knx][session][unit]")
    {
        tunnelling_session session;
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        REQUIRE(session.begin_request(3u, 5u, 100u).has_value());

        std::array<std::uint8_t, 10u> packet {};
        REQUIRE(frame::encode_tunnelling_ack_packet(packet, 3u, 6u).has_value());
        const auto result = session.dispatch_session_datagram(packet, 700u);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::sequence_error));
        CHECK(session.has_pending_request());
        CHECK(session.last_activity_ms() == 0u);
    }

    TEST_CASE("knx session ignores unrelated dispatched services", "[knx][session][unit]")
    {
        tunnelling_session session;
        REQUIRE(session.begin_request(3u, 5u, 100u).has_value());

        const datagram unrelated {
            .service_type = connection::connectionstate_request_service,
            .payload = connectionstate_request_frame { 3u },
        };
        const auto result = session.on_datagram(unrelated);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::invalid_configuration));
        CHECK(session.state() == session_state::waiting_ack);
        CHECK(session.has_pending_request());
    }

    TEST_CASE("knx session rejects typed datagrams with mismatched services", "[knx][session][unit]")
    {
        tunnelling_session session;
        REQUIRE(session.begin_request(3u, 5u, 100u).has_value());

        const datagram mismatched_ack {
            .service_type = connection::connectionstate_response_service,
            .payload = tunnelling_ack_frame { 3u, 5u, 0u },
        };
        const auto ack_result = session.on_datagram(mismatched_ack);
        REQUIRE(!ack_result.has_value());
        CHECK(ack_result.error() == make_error_code(error::invalid_configuration));
        CHECK(session.has_pending_request());

        const datagram mismatched_connect {
            .service_type = connection::connect_request_service,
            .payload = connect_response_frame { 3u, connect_status::no_error, {} },
        };
        tunnelling_session idle_session;
        const auto connect_result = idle_session.on_datagram(mismatched_connect);
        REQUIRE(!connect_result.has_value());
        CHECK(connect_result.error() == make_error_code(error::invalid_configuration));
        CHECK(idle_session.state() == session_state::idle);
    }

    TEST_CASE("knx session prepares heartbeat only while connected", "[knx][session][integration]")
    {
        tunnelling_session session;
        std::array<std::uint8_t, 8u> packet {};

        const auto before_connect = session.prepare_connectionstate_request_packet(packet);
        REQUIRE(!before_connect.has_value());
        CHECK(before_connect.error() == make_error_code(error::shutdown));

        REQUIRE(session.begin_request(3u, 5u, 100u).has_value());
        REQUIRE(session.on_ack(3u, 5u).has_value());
        REQUIRE(session.prepare_connectionstate_request_packet(packet).has_value());
        CHECK(packet[2] == 0x02u);
        CHECK(packet[3] == 0x07u);
        CHECK(packet[6] == 3u);
    }

    TEST_CASE("knx session accepts a successful heartbeat response", "[knx][session][integration]")
    {
        tunnelling_session session;
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());

        const connectionstate_response_frame response { 3u, connect_status::no_error };
        REQUIRE(session.on_connectionstate_response(response).has_value());
        CHECK(session.state() == session_state::connected);
        CHECK(session.channel_id() == 3u);
    }

    TEST_CASE("knx session rejects a failed or mismatched heartbeat", "[knx][session][unit]")
    {
        tunnelling_session session;
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());

        const auto failed = session.on_connectionstate_response(
            connectionstate_response_frame { 3u, connect_status::connection_type });
        REQUIRE(!failed.has_value());
        CHECK(failed.error() == make_error_code(error::connection_failed));
        CHECK(session.state() == session_state::connected);

        const auto mismatched = session.on_connectionstate_response(
            connectionstate_response_frame { 4u, connect_status::no_error });
        REQUIRE(!mismatched.has_value());
        CHECK(mismatched.error() == make_error_code(error::sequence_error));
        CHECK(session.state() == session_state::connected);
    }

    TEST_CASE("knx session escalates consecutive heartbeat failures", "[knx][session][unit]")
    {
        tunnelling_session session {{ .max_retries = 2u, .ack_timeout_ms = 1000u, .heartbeat_failure_limit = 3u }};
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        const connectionstate_response_frame failed { 3u, connect_status::connection_type };

        REQUIRE(!session.on_connectionstate_response(failed).has_value());
        CHECK(session.heartbeat_failures() == 1u);
        REQUIRE(!session.on_connectionstate_response(failed).has_value());
        CHECK(session.heartbeat_failures() == 2u);
        const auto terminal = session.on_connectionstate_response(failed);
        REQUIRE(!terminal.has_value());
        CHECK(terminal.error() == make_error_code(error::heartbeat_failed));
        CHECK(session.state() == session_state::closed);
    }

    TEST_CASE("knx session resets heartbeat failures after success", "[knx][session][unit]")
    {
        tunnelling_session session {{ .max_retries = 2u, .ack_timeout_ms = 1000u, .heartbeat_failure_limit = 2u }};
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        REQUIRE(!session.on_connectionstate_response(
            connectionstate_response_frame { 3u, connect_status::connection_type }).has_value());
        REQUIRE(session.heartbeat_failures() == 1u);
        REQUIRE(session.on_connectionstate_response(
            connectionstate_response_frame { 3u, connect_status::no_error }).has_value());
        CHECK(session.heartbeat_failures() == 0u);
        CHECK(session.state() == session_state::connected);
    }

    TEST_CASE("knx session detects and closes on inactivity", "[knx][session][unit]")
    {
        tunnelling_session session {{ .max_retries = 2u,
                                      .ack_timeout_ms = 1000u,
                                      .heartbeat_failure_limit = 3u,
                                      .inactivity_timeout_ms = 100u }};
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        session.note_activity(1'000u);
        CHECK(!session.inactive(1'099u));
        CHECK(session.inactive(1'100u));

        const auto result = session.check_inactivity(1'100u);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::inactivity_timeout));
        CHECK(session.state() == session_state::closed);
    }

    TEST_CASE("knx session handles inactivity timestamp rollover", "[knx][session][unit]")
    {
        tunnelling_session session {{ .inactivity_timeout_ms = 32u }};
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        session.note_activity(0xFFFF'FFF0u);
        CHECK(!session.inactive(0x0000'000Fu));
        CHECK(session.inactive(0x0000'0010u));
    }

    TEST_CASE("knx session records activity after valid datagram ingress", "[knx][session][integration]")
    {
        tunnelling_session session {{ .inactivity_timeout_ms = 100u }};
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        CHECK(session.last_activity_ms() == 0u);

        std::array<std::uint8_t, 8u> heartbeat {};
        REQUIRE(connection::encode_connectionstate_response_packet(
                    heartbeat, connectionstate_response_frame { 3u, connect_status::no_error }).has_value());
        REQUIRE(session.on_datagram_at(heartbeat, 1'000u).has_value());
        CHECK(session.last_activity_ms() == 1'000u);
        CHECK(!session.inactive(1'099u));
    }

    TEST_CASE("knx session does not record malformed datagram activity", "[knx][session][unit]")
    {
        tunnelling_session session {{ .inactivity_timeout_ms = 100u }};
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        const std::array<std::uint8_t, 6u> malformed {
            0x06u, 0x10u, 0x02u, 0x08u, 0x00u, 0x05u,
        };

        const auto result = session.on_datagram_at(malformed, 1'000u);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::malformed_frame));
        CHECK(session.last_activity_ms() == 0u);
    }

    TEST_CASE("knx session dispatches all session control traffic with activity", "[knx][session][integration]")
    {
        tunnelling_session session;
        const datagram connect {
            .service_type = connection::connect_response_service,
            .payload = connect_response_frame { 3u, connect_status::no_error, {} },
        };
        REQUIRE(session.dispatch_session_datagram(connect, 10u).has_value());
        CHECK(session.state() == session_state::connected);
        CHECK(session.last_activity_ms() == 10u);

        const datagram heartbeat {
            .service_type = connection::connectionstate_response_service,
            .payload = connectionstate_response_frame { 3u, connect_status::no_error },
        };
        REQUIRE(session.dispatch_session_datagram(heartbeat, 20u).has_value());
        CHECK(session.last_activity_ms() == 20u);

        const datagram discovery {
            .service_type = discovery::search_request_service,
            .payload = discovery::search_request_frame {},
        };
        const auto unrelated = session.dispatch_session_datagram(discovery, 30u);
        REQUIRE(!unrelated.has_value());
        CHECK(unrelated.error() == make_error_code(error::unsupported_service));
        CHECK(session.last_activity_ms() == 20u);
    }

    TEST_CASE("knx session dispatches matching inbound tunnelling requests", "[knx][session][integration]")
    {
        tunnelling_session session;
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        const auto& cemi = sample_cemi;
        std::array<std::uint8_t, sample_tunnelling_packet_size> packet {};
        REQUIRE(frame::encode_tunnelling_request_packet(packet, 3u, 9u, cemi).has_value());

        REQUIRE(session.dispatch_session_datagram(packet, 100u).has_value());
        CHECK(session.last_activity_ms() == 100u);
    }

    TEST_CASE("knx session rejects inbound tunnelling requests for another channel", "[knx][session][unit]")
    {
        tunnelling_session session;
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        const auto& cemi = sample_cemi;
        std::array<std::uint8_t, sample_tunnelling_packet_size> packet {};
        REQUIRE(frame::encode_tunnelling_request_packet(packet, 4u, 9u, cemi).has_value());

        const auto result = session.dispatch_session_datagram(packet, 100u);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::sequence_error));
        CHECK(session.last_activity_ms() == 0u);
    }

    TEST_CASE("knx session validates and responds to inbound tunnelling requests", "[knx][session][integration]")
    {
        tunnelling_session session;
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        const auto& cemi = sample_cemi;
        std::array<std::uint8_t, sample_tunnelling_packet_size> request_packet {};
        REQUIRE(frame::encode_tunnelling_request_packet(request_packet, 3u, 9u, cemi).has_value());

        std::array<std::uint8_t, 10u> response_packet {};
        REQUIRE(session.prepare_response_datagram(response_packet, request_packet).has_value());
        const auto response = frame::decode_tunnelling_ack_packet(response_packet);
        REQUIRE(response.has_value());
        CHECK(response->channel_id == 3u);
        CHECK(response->sequence_number == 9u);
    }

    TEST_CASE("knx session preserves state when response preparation rejects a request", "[knx][session][unit]")
    {
        tunnelling_session session;
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        const datagram heartbeat {
            .service_type = connection::connectionstate_request_service,
            .payload = connectionstate_request_frame { 4u },
        };
        std::array<std::uint8_t, 8u> response_packet {};

        const auto result = session.prepare_response_datagram(response_packet, heartbeat);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::sequence_error));
        CHECK(session.state() == session_state::connected);
    }

    TEST_CASE("knx session responds atomically to a matching disconnect request", "[knx][session][integration]")
    {
        tunnelling_session session;
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        const datagram request {
            .service_type = connection::disconnect_request_service,
            .payload = disconnect_request_frame { 3u },
        };
        std::array<std::uint8_t, 8u> response_packet {};
        REQUIRE(session.prepare_response_datagram(response_packet, request).has_value());
        CHECK(session.state() == session_state::closing);

        const auto response = connection::decode_disconnect_response_packet(response_packet);
        REQUIRE(response.has_value());
        CHECK(response->channel_id == 3u);
        CHECK(response->status == connect_status::no_error);
    }

    TEST_CASE("knx session dispatches and answers a raw heartbeat request", "[knx][session][integration]")
    {
        tunnelling_session session;
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());

        std::array<std::uint8_t, 8u> request_packet {};
        REQUIRE(connection::encode_connectionstate_request_packet(
                    request_packet, connectionstate_request_frame { 3u }).has_value());
        REQUIRE(session.dispatch_session_datagram(request_packet, 400u).has_value());
        CHECK(session.last_activity_ms() == 400u);

        std::array<std::uint8_t, 8u> response_packet {};
        REQUIRE(session.prepare_response_datagram(response_packet, request_packet).has_value());
        const auto response = connection::decode_connectionstate_response_packet(response_packet);
        REQUIRE(response.has_value());
        CHECK(response->channel_id == 3u);
        CHECK(response->status == connect_status::no_error);
    }

    TEST_CASE("knx session rejects disconnect requests on another channel", "[knx][session][unit]")
    {
        tunnelling_session session;
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        const datagram request {
            .service_type = connection::disconnect_request_service,
            .payload = disconnect_request_frame { 4u },
        };
        std::array<std::uint8_t, 8u> response_packet {};
        const auto result = session.prepare_response_datagram(response_packet, request);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::sequence_error));
        CHECK(session.state() == session_state::connected);
    }

    TEST_CASE("knx response preparation keeps state on encoding failure", "[knx][session][unit]")
    {
        tunnelling_session session;
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        const datagram request {
            .service_type = connection::disconnect_request_service,
            .payload = disconnect_request_frame { 3u },
        };
        std::array<std::uint8_t, 7u> too_small {};

        const auto result = session.prepare_response_datagram(too_small, request);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::invalid_length));
        CHECK(session.state() == session_state::connected);
    }

    TEST_CASE("knx response preparation does not write rejected requests", "[knx][session][unit]")
    {
        tunnelling_session session;
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        const datagram request {
            .service_type = connection::disconnect_request_service,
            .payload = disconnect_request_frame { 4u },
        };
        std::array<std::uint8_t, 8u> response_packet {};
        response_packet.fill(0xA5u);
        const auto result = session.prepare_response_datagram(response_packet, request);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::sequence_error));
        CHECK(std::all_of(response_packet.begin(), response_packet.end(),
                          [](const std::uint8_t value) { return value == 0xA5u; }));
    }

    TEST_CASE("knx session dispatches and answers a raw disconnect request", "[knx][session][integration]")
    {
        tunnelling_session session;
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());

        std::array<std::uint8_t, 8u> request_packet {};
        REQUIRE(connection::encode_disconnect_request_packet(
                    request_packet, disconnect_request_frame { 3u }).has_value());
        REQUIRE(session.dispatch_session_datagram(request_packet, 500u).has_value());
        CHECK(session.state() == session_state::closing);
        CHECK(session.last_activity_ms() == 0u);

        std::array<std::uint8_t, 8u> response_packet {};
        REQUIRE(session.prepare_response_datagram(response_packet, request_packet).has_value());
        const auto response = connection::decode_disconnect_response_packet(response_packet);
        REQUIRE(response.has_value());
        CHECK(response->channel_id == 3u);
        CHECK(response->status == connect_status::no_error);
    }

    TEST_CASE("knx session dispatches a raw disconnect response while closing", "[knx][session][integration]")
    {
        tunnelling_session session;
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        std::array<std::uint8_t, 8u> request_packet {};
        REQUIRE(session.prepare_disconnect_request_packet(request_packet).has_value());

        std::array<std::uint8_t, 8u> response_packet {};
        REQUIRE(connection::encode_disconnect_response_packet(
                    response_packet, disconnect_response_frame { 3u, connect_status::no_error }).has_value());
        REQUIRE(session.dispatch_session_datagram(response_packet, 800u).has_value());
        CHECK(session.state() == session_state::closed);
        CHECK(session.last_activity_ms() == 0u);

        const auto late = session.dispatch_session_datagram(response_packet, 801u);
        REQUIRE(!late.has_value());
        CHECK(late.error() == make_error_code(error::shutdown));
    }

    TEST_CASE("knx session consumes a heartbeat response packet", "[knx][session][integration]")
    {
        tunnelling_session session;
        REQUIRE(session.on_connect_response(connect_response_frame { 3u, connect_status::no_error, {} }).has_value());

        std::array<std::uint8_t, 8u> packet {};
        REQUIRE(connection::encode_connectionstate_response_packet(
                    packet, connectionstate_response_frame { 3u, connect_status::no_error }).has_value());
        REQUIRE(session.on_connectionstate_response_packet(packet).has_value());
        CHECK(session.state() == session_state::connected);
    }

    TEST_CASE("knx session enters connected after a successful connect response", "[knx][session][integration]")
    {
        tunnelling_session session;
        const connect_response_frame response {
            .channel_id = 7u,
            .status = connect_status::no_error,
        };

        REQUIRE(session.on_connect_response(response).has_value());
        CHECK(session.state() == session_state::connected);
        CHECK(session.channel_id() == 7u);
        REQUIRE(session.begin_request(7u, 42u, 100u).has_value());
    }

    TEST_CASE("knx session preserves idle state after failed connect response", "[knx][session][unit]")
    {
        tunnelling_session session;
        const connect_response_frame response {
            .channel_id = 7u,
            .status = connect_status::no_more_connections,
        };

        const auto result = session.on_connect_response(response);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::connection_failed));
        CHECK(session.state() == session_state::idle);
        CHECK(session.channel_id() == 0u);
    }

    TEST_CASE("knx session consumes a complete connect response packet", "[knx][session][integration]")
    {
        tunnelling_session session;
        const connect_response_frame response {
            .channel_id = 8u,
            .status = connect_status::no_error,
            .data_endpoint = hpai { ipv4_endpoint { { 127u, 0u, 0u, 1u }, 3672u }, 0x01u },
        };
        std::array<std::uint8_t, 20u> packet {};
        REQUIRE(connection::encode_connect_response_packet(packet, response).has_value());
        REQUIRE(session.on_connect_response_packet(packet).has_value());
        CHECK(session.state() == session_state::connected);
        CHECK(session.channel_id() == 8u);
    }

    TEST_CASE("knx session prepares a connect request while idle", "[knx][session][integration]")
    {
        tunnelling_session session;
        const connect_request_frame request {
            .control_endpoint = hpai { ipv4_endpoint { { 127u, 0u, 0u, 1u }, 3671u }, 0x01u },
            .data_endpoint = hpai { ipv4_endpoint { { 127u, 0u, 0u, 1u }, 3672u }, 0x01u },
        };
        std::array<std::uint8_t, 26u> packet {};

        REQUIRE(session.prepare_connect_request_packet(packet, request).has_value());
        CHECK(session.state() == session_state::idle);
        const auto decoded = connection::decode_connect_request_packet(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->data_endpoint.endpoint.port == 3672u);

    }

    TEST_CASE("knx session tracks a timed connection attempt", "[knx][session][integration]")
    {
        // A connect retry extends the deadline by the connect timeout, which is its own value: the
        // tunnelling acknowledgement timeout is an order of magnitude shorter and is not this clock.
        tunnelling_session session {{ .max_retries = 2u, .ack_timeout_ms = 10u, .connect_timeout_ms = 100u }};
        const connect_request_frame request {
            .control_endpoint = hpai { ipv4_endpoint { { 127u, 0u, 0u, 1u }, 3671u }, 0x01u },
            .data_endpoint = hpai { ipv4_endpoint { { 127u, 0u, 0u, 1u }, 3672u }, 0x01u },
        };
        std::array<std::uint8_t, 26u> packet {};
        REQUIRE(session.start_connect(packet, request, 1'000u).has_value());
        CHECK(session.state() == session_state::connecting);
        CHECK(session.active_connect_packet().size() == packet.size());
        CHECK(!session.connect_expired(999u));
        CHECK(session.connect_expired(1'000u));

        REQUIRE(session.on_connect_timeout().has_value());
        CHECK(session.connect_expired(1'100u));
        std::array<std::uint8_t, 26u> retry {};
        REQUIRE(session.prepare_connect_retry_packet(retry).has_value());
        CHECK(retry == packet);
        REQUIRE(session.on_connect_response(connect_response_frame { 7u, connect_status::no_error, {} }).has_value());
        CHECK(session.state() == session_state::connected);
        CHECK(session.active_connect_packet().empty());
    }

    TEST_CASE("knx session closes after connect retry exhaustion", "[knx][session][unit]")
    {
        tunnelling_session session {{ .max_retries = 1u, .ack_timeout_ms = 10u }};
        const connect_request_frame request {};
        std::array<std::uint8_t, 26u> packet {};
        REQUIRE(session.start_connect(packet, request, 100u).has_value());
        REQUIRE(session.on_connect_timeout().has_value());
        const auto terminal = session.on_connect_timeout();
        REQUIRE(!terminal.has_value());
        CHECK(terminal.error() == make_error_code(error::timeout));
        CHECK(session.state() == session_state::closed);
        CHECK(!session.prepare_connect_retry_packet(packet).has_value());
    }

    TEST_CASE("knx session reset permits reuse after terminal failure", "[knx][session][unit]")
    {
        tunnelling_session session {{ .max_retries = 0u, .ack_timeout_ms = 10u }};
        const connect_request_frame request {};
        std::array<std::uint8_t, 26u> packet {};
        REQUIRE(session.start_connect(packet, request, 100u).has_value());
        REQUIRE(!session.on_connect_timeout().has_value());
        CHECK(session.state() == session_state::closed);

        session.reset();
        CHECK(session.state() == session_state::idle);
        CHECK(session.channel_id() == 0u);
        CHECK(session.heartbeat_failures() == 0u);
        CHECK(session.last_activity_ms() == 0u);
        CHECK(session.active_connect_packet().empty());
        REQUIRE(session.start_connect(packet, request, 200u).has_value());
        CHECK(session.state() == session_state::connecting);
    }

    TEST_CASE("knx session reset clears active tunnelling operation", "[knx][session][unit]")
    {
        tunnelling_session session {{ .max_retries = 2u, .ack_timeout_ms = 10u }};
        const auto& cemi = sample_cemi;
        std::array<std::uint8_t, sample_tunnelling_packet_size> packet {};
        REQUIRE(session.prepare_request_packet(packet, 3u, cemi, 100u).has_value());
        REQUIRE(session.on_timeout().has_value());
        CHECK(session.has_pending_request());
        CHECK(!session.active_request_packet().empty());
        CHECK(session.retries() == 1u);

        session.reset();
        CHECK(session.state() == session_state::idle);
        CHECK(!session.has_pending_request());
        CHECK(session.retries() == 0u);
        CHECK(session.next_sequence() == 0u);
        CHECK(session.active_request_packet().empty());
        CHECK(session.active_connect_packet().empty());

        const auto sequence = session.begin_request(3u, 100u);
        REQUIRE(sequence.has_value());
        CHECK(sequence.value() == 0u);
    }

    TEST_CASE("knx session reset recovers every terminal connected state", "[knx][session][unit]")
    {
        tunnelling_session heartbeat_session {{ .heartbeat_failure_limit = 1u }};
        REQUIRE(heartbeat_session.on_connect_response(
            connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        REQUIRE(!heartbeat_session.on_connectionstate_response(
            connectionstate_response_frame { 3u, connect_status::connection_type }).has_value());
        CHECK(heartbeat_session.state() == session_state::closed);
        heartbeat_session.reset();
        CHECK(heartbeat_session.state() == session_state::idle);

        tunnelling_session inactivity_session {{ .inactivity_timeout_ms = 1u }};
        REQUIRE(inactivity_session.on_connect_response(
            connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        REQUIRE(!inactivity_session.check_inactivity(1u).has_value());
        CHECK(inactivity_session.state() == session_state::closed);
        inactivity_session.reset();
        CHECK(inactivity_session.state() == session_state::idle);

        tunnelling_session disconnect_session;
        REQUIRE(disconnect_session.on_connect_response(
            connect_response_frame { 3u, connect_status::no_error, {} }).has_value());
        std::array<std::uint8_t, 8u> disconnect_packet {};
        REQUIRE(disconnect_session.prepare_disconnect_request_packet(disconnect_packet).has_value());
        const std::array<std::uint8_t, 8u> response {
            0x06u, 0x10u, 0x02u, 0x0Au, 0x00u, 0x08u, 0x03u, 0x00u,
        };
        REQUIRE(disconnect_session.on_disconnect_response_packet(response).has_value());
        CHECK(disconnect_session.state() == session_state::closed);
        disconnect_session.reset();
        CHECK(disconnect_session.state() == session_state::idle);
    }

    TEST_CASE("knx session dispatches a connect response", "[knx][session][integration]")
    {
        tunnelling_session session;
        const datagram response {
            .service_type = connection::connect_response_service,
            .payload = connect_response_frame { 6u, connect_status::no_error, {} },
        };

        REQUIRE(session.on_datagram(response).has_value());
        CHECK(session.state() == session_state::connected);
        CHECK(session.channel_id() == 6u);
    }

    TEST_CASE("knx session dispatches a raw connect response with activity", "[knx][session][integration]")
    {
        tunnelling_session session;
        const connect_response_frame response {
            .channel_id = 6u,
            .status = connect_status::no_error,
            .data_endpoint = hpai { ipv4_endpoint { { 127u, 0u, 0u, 1u }, 3672u }, 0x01u },
        };
        std::array<std::uint8_t, 20u> packet {};
        REQUIRE(connection::encode_connect_response_packet(packet, response).has_value());

        REQUIRE(session.dispatch_session_datagram(packet, 600u).has_value());
        CHECK(session.state() == session_state::connected);
        CHECK(session.channel_id() == 6u);
        CHECK(session.last_activity_ms() == 600u);
    }

    TEST_CASE("knx session does not mutate on malformed connect response", "[knx][session][unit]")
    {
        tunnelling_session session;
        const std::array<std::uint8_t, 6u> packet {
            0x06u, 0x10u, 0x02u, 0x06u, 0x00u, 0x05u,
        };
        const auto result = session.dispatch_session_datagram(packet, 600u);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::malformed_frame));
        CHECK(session.state() == session_state::idle);
        CHECK(session.last_activity_ms() == 0u);
    }

    TEST_CASE("knx session clears connect retry data after connection", "[knx][session][unit]")
    {
        tunnelling_session session;
        const connect_request_frame request {
            .control_endpoint = hpai { ipv4_endpoint { { 127u, 0u, 0u, 1u }, 3671u }, 0x01u },
            .data_endpoint = hpai { ipv4_endpoint { { 127u, 0u, 0u, 1u }, 3672u }, 0x01u },
        };
        std::array<std::uint8_t, 26u> packet {};
        REQUIRE(session.prepare_connect_request_packet(packet, request).has_value());
        REQUIRE(session.on_connect_response(connect_response_frame { 5u, connect_status::no_error, {} }).has_value());

        std::array<std::uint8_t, 26u> retry {};
        const auto result = session.prepare_connect_retry_packet(retry);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::invalid_configuration));
    }

    TEST_CASE("knx session prepares and completes graceful disconnect", "[knx][session][integration]")
    {
        tunnelling_session session;
        REQUIRE(session.begin_request(3u, 5u, 100u).has_value());
        REQUIRE(session.on_ack(3u, 5u).has_value());

        std::array<std::uint8_t, 8u> request_packet {};
        REQUIRE(session.prepare_disconnect_request_packet(request_packet).has_value());
        CHECK(session.state() == session_state::closing);
        CHECK(request_packet[2] == 0x02u);
        CHECK(request_packet[3] == 0x09u);
        CHECK(request_packet[6] == 3u);
        REQUIRE(!session.prepare_disconnect_request_packet(request_packet).has_value());

        const std::array<std::uint8_t, 8u> response_packet {
            0x06u, 0x10u, 0x02u, 0x0Au, 0x00u, 0x08u, 0x03u, 0x00u,
        };
        REQUIRE(session.on_disconnect_response_packet(response_packet).has_value());
        CHECK(session.state() == session_state::closed);
        const auto late = session.on_disconnect_response_packet(response_packet);
        REQUIRE(!late.has_value());
        CHECK(late.error() == make_error_code(error::shutdown));
    }

    TEST_CASE("knx session acknowledges an inbound tunnelling request", "[knx][session][integration]")
    {
        tunnelling_session session;
        const auto& cemi = sample_cemi;
        std::array<std::uint8_t, sample_tunnelling_packet_size> request_packet {};
        REQUIRE(frame::encode_tunnelling_request_packet(request_packet, 4u, 9u, cemi).has_value());

        std::array<std::uint8_t, 10u> ack_packet {};
        REQUIRE(session.prepare_tunnelling_ack_packet(ack_packet, request_packet).has_value());
        const auto ack = frame::decode_tunnelling_ack_packet(ack_packet);
        REQUIRE(ack.has_value());
        CHECK(ack->channel_id == 4u);
        CHECK(ack->sequence_number == 9u);
        CHECK(ack->status == 0u);
        CHECK(session.state() == session_state::idle);

        REQUIRE(session.prepare_tunnelling_ack_packet(ack_packet, request_packet, 0x21u).has_value());
        const auto negative_ack = frame::decode_tunnelling_ack_packet(ack_packet);
        REQUIRE(negative_ack.has_value());
        CHECK(negative_ack->channel_id == 4u);
        CHECK(negative_ack->sequence_number == 9u);
        CHECK(negative_ack->status == 0x21u);
    }

    TEST_CASE("knx session rejects malformed inbound tunnelling requests", "[knx][session][unit]")
    {
        tunnelling_session session;
        const std::array<std::uint8_t, 6u> request_packet {
            0x06u, 0x10u, 0x04u, 0x20u, 0x00u, 0x06u,
        };
        std::array<std::uint8_t, 10u> ack_packet {};

        const auto result = session.prepare_tunnelling_ack_packet(ack_packet, request_packet);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::malformed_frame));
        CHECK(session.state() == session_state::idle);
    }
}
