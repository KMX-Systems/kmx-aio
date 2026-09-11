/// @file src/kmx/aio/knx/tunnelling_session_stream_test.cpp
/// @brief The tunnelling session under the rules of KNXnet/IP over TCP.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#ifndef PCH
    #include <kmx/aio/knx/connection.hpp>
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/frame.hpp>
    #include <kmx/aio/knx/session.hpp>
    #include <kmx/aio/knx/tunnelling_session.hpp>
    #include <kmx/aio/test/knx/telegram.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <array>
    #include <cstdint>
#endif

namespace kmx::aio::test::knx::tunnelling_session_stream_test
{
    using namespace kmx::aio::knx;

    namespace detail
    {
        /// @brief The channel every session here is connected on.
        constexpr std::uint8_t channel = 7u;

        /// @brief The TCP HPAI: protocol `0x02`, no address, no port.
        constexpr hpai tcp_endpoint {{}, 0x02u};

        /// @brief Returns a session under stream rules, connected on @ref channel.
        [[nodiscard]] tunnelling_session connected_over_tcp(const tunnelling_config& config = {})
        {
            tunnelling_session session {config};
            session.use_stream_rules(true);
            const connect_response_frame response {
                .channel_id = channel, .status = connect_status::no_error, .data_endpoint = tcp_endpoint};
            REQUIRE(session.on_connect_response(response).has_value());
            return session;
        }

        /// @brief Encodes a TUNNELLING_REQUEST on @ref channel carrying @ref sample_cemi.
        [[nodiscard]] std::array<std::uint8_t, sample_tunnelling_packet_size> indication(const std::uint8_t sequence)
        {
            std::array<std::uint8_t, sample_tunnelling_packet_size> packet {};
            REQUIRE(frame::encode_tunnelling_request_packet(packet, channel, sequence, sample_cemi).has_value());
            return packet;
        }
    }

    TEST_CASE("knx session under stream rules completes a request once it is prepared", "[knx][session][tcp][unit]")
    {
        auto session = detail::connected_over_tcp();
        std::array<std::uint8_t, sample_tunnelling_packet_size> packet {};

        // No acknowledgement is awaited, so nothing is outstanding, retained or retried - but each request still
        // takes the next sequence number.
        for (std::uint8_t sequence = 0u; sequence < 3u; ++sequence)
        {
            const auto prepared = session.prepare_request_packet(packet, detail::channel, sample_cemi, 1'000u);
            REQUIRE(prepared.has_value());
            CHECK(*prepared == sequence);
            CHECK(session.state() == session_state::connected);
            CHECK(!session.has_pending_request());
            CHECK(session.active_request_packet().empty());
        }

        CHECK(session.next_sequence() == 3u);
        CHECK(!session.expired(1'000'000u));
        const auto timeout = session.on_timeout();
        REQUIRE(!timeout.has_value());
        CHECK(timeout.error() == make_error_code(error::invalid_configuration));
    }

    TEST_CASE("knx session under stream rules accepts indications in any order", "[knx][session][tcp][unit]")
    {
        auto session = detail::connected_over_tcp();

        // Out of order, then repeated: both refused under UDP rules, and both simply delivered over a stream.
        for (const auto sequence: std::array<std::uint8_t, 4u> {5u, 2u, 2u, 200u})
        {
            const auto packet = detail::indication(sequence);
            const auto request = frame::decode_tunnelling_request_packet(packet);
            REQUIRE(request.has_value());
            CHECK(!session.duplicate_indication(*request));
            CHECK(!session.out_of_order_indication(*request));
            CHECK(session.on_datagram(packet).has_value());
        }

        CHECK(session.state() == session_state::connected);
    }

    TEST_CASE("knx session under stream rules attempts a connect and a disconnect once", "[knx][session][tcp][unit]")
    {
        tunnelling_session session {{.max_retries = 5u}};
        session.use_stream_rules(true);
        const connect_request_frame request {.control_endpoint = detail::tcp_endpoint, .data_endpoint = detail::tcp_endpoint};
        std::array<std::uint8_t, 26u> packet {};
        REQUIRE(session.start_connect(packet, request, 100u).has_value());
        const auto connect = session.on_connect_timeout();
        REQUIRE(!connect.has_value());
        CHECK(connect.error() == make_error_code(error::timeout));
        CHECK(session.state() == session_state::closed);

        // The rules describe the transport, so a reset keeps them.
        session.reset();
        CHECK(session.stream_rules());
        const connect_response_frame response {
            .channel_id = detail::channel, .status = connect_status::no_error, .data_endpoint = detail::tcp_endpoint};
        REQUIRE(session.on_connect_response(response).has_value());
        std::array<std::uint8_t, 16u> disconnect {};
        REQUIRE(session.prepare_disconnect_request_packet(disconnect).has_value());
        const auto closing = session.on_disconnect_timeout();
        REQUIRE(!closing.has_value());
        CHECK(closing.error() == make_error_code(error::timeout));
        CHECK(session.state() == session_state::closed);
    }

    TEST_CASE("knx session counts an unanswered heartbeat once its deadline passes", "[knx][session][tcp][unit]")
    {
        auto session = detail::connected_over_tcp({.heartbeat_failure_limit = 2u});

        // A second heartbeat sent while the first is unanswered keeps the first one's deadline.
        session.note_heartbeat_sent(1'000u);
        session.note_heartbeat_sent(5'000u);
        CHECK(session.heartbeat_outstanding());
        CHECK(session.check_heartbeat(999u).has_value());
        const auto missed = session.check_heartbeat(1'000u);
        REQUIRE(!missed.has_value());
        CHECK(missed.error() == make_error_code(error::connection_failed));
        CHECK(!session.heartbeat_outstanding());
        CHECK(session.check_heartbeat(10'000u).has_value());

        // An answer clears the heartbeat it answers, and a successful one clears the failure count as well.
        session.note_heartbeat_sent(2'000u);
        REQUIRE(
            session.on_connectionstate_response(connectionstate_response_frame {detail::channel, connect_status::no_error}).has_value());
        CHECK(!session.heartbeat_outstanding());
        CHECK(session.heartbeat_failures() == 0u);
        CHECK(session.check_heartbeat(10'000u).has_value());

        // Reaching the failure limit closes the session, and a closed session has no heartbeat outstanding.
        session.note_heartbeat_sent(20'000u);
        REQUIRE(!session.check_heartbeat(20'000u).has_value());
        session.note_heartbeat_sent(30'000u);
        const auto limit = session.check_heartbeat(30'000u);
        REQUIRE(!limit.has_value());
        CHECK(limit.error() == make_error_code(error::heartbeat_failed));
        CHECK(session.state() == session_state::closed);
        CHECK(!session.heartbeat_outstanding());
    }
}
