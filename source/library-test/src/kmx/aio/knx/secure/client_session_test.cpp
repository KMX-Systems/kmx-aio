/// @file kmx/aio/knx/secure/client_session_test.cpp
/// @brief The KNX IP Secure client session: xknx's handshake end to end, refused frames, and its clock.
/// @details The client's key pair is xknx's fixture, handed out by a fixed-key entropy source, so the handshake the
/// session runs is the one whose MACs xknx computes. The server side is played with the session key derived from the
/// same fixture.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/error.hpp>
#include <kmx/aio/knx/frame.hpp>
#include <kmx/aio/knx/secure/client_session.hpp>
#include <kmx/aio/knx/secure/detail/ccm.hpp>
#include <kmx/aio/test/knx/secure_vectors.hpp>
#include <kmx/aio/test/knx/telegram.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace kmx::aio::test::knx::secure::client_session_test
{
    namespace kn = kmx::aio::knx;
    namespace ks = kmx::aio::knx::secure;
    namespace sv = kmx::aio::test::knx::secure_vectors;
    namespace kd = kmx::aio::knx::secure::detail;
    using kn::error;
    using kn::make_error_code;

    namespace detail
    {
        /// @brief A buffer as large as any datagram.
        using buffer_t = std::array<std::uint8_t, kn::frame::max_datagram_size>;

        /// @brief The client's serial number, and the server's.
        constexpr ks::serial_number_t client_serial {0x00u, 0xFAu, 0x12u, 0x34u, 0x56u, 0x78u};
        constexpr ks::serial_number_t server_serial {0x00u, 0xFAu, 0xAAu, 0xAAu, 0xAAu, 0xAAu};

        /// @brief xknx's session fixture.
        struct handshake_fixture
        {
            ks::x25519_private_key client_private {
                sv::fixed<32u>("b8 fa bd 62 66 5d 8b 9e 8a 9d 8b 1f 4b ca 42 c8 c2 78 9a 61 10 f5 0e 9d d7 85 b3 ed e8 83 f3 78")};
            ks::x25519_public_key_t client_public {
                sv::fixed<32u>("0a a2 27 b4 fd 7a 32 31 9b a9 96 0a c0 36 ce 0e 5c 45 07 b5 ae 55 16 1f 10 78 b1 dc fb 3c b6 31")};
            ks::x25519_public_key_t server_public {
                sv::fixed<32u>("bd f0 99 90 99 23 14 3e f0 a5 de 0b 3b e3 68 7b c5 bd 3c f5 f9 e6 f9 01 69 9c d8 70 ec 1f f8 24")};
        };

        /// @brief Hands out the fixture's key pair, counting how often one was asked for.
        class fixed_key_entropy final: public ks::entropy_source
        {
        public:
            explicit fixed_key_entropy(const handshake_fixture& fixture) noexcept: fixture_(fixture) {}

            /// @brief How many key pairs were drawn.
            std::size_t pairs {};

            [[nodiscard]] expected_void_t fill(const span_uint8_t destination) noexcept override
            {
                std::ranges::fill(destination, std::uint8_t {});
                return {};
            }

            [[nodiscard]] ks::x25519_key_pair_result_t generate_key_pair() noexcept override
            {
                ++pairs;
                return ks::x25519_key_pair {.private_key = fixture_.client_private.clone(), .public_key = fixture_.client_public};
            }

        private:
            const handshake_fixture& fixture_;
        };

        /// @brief Returns the error a result carries, or no error.
        template <typename Value>
        [[nodiscard]] std::error_code error_of(const std::expected<Value, std::error_code>& result) noexcept
        {
            return result.has_value() ? std::error_code {} : result.error();
        }

        [[nodiscard]] ks::tunnelling_credentials credentials(const bool skip_device_authentication = false)
        {
            auto user_key = ks::derive_user_password_key("secret");
            auto device_code = ks::derive_device_authentication_code("trustme");
            REQUIRE(user_key.has_value());
            REQUIRE(device_code.has_value());
            return ks::tunnelling_credentials {.user_id = 1u,
                                               .user_password_key = std::move(*user_key),
                                               .device_authentication_code = std::move(*device_code),
                                               .skip_device_authentication = skip_device_authentication,
                                               .serial_number = client_serial};
        }

        /// @brief The server's SESSION_RESPONSE for session 1, with its genuine MAC.
        [[nodiscard]] ks::session_response_frame response(const handshake_fixture& fixture)
        {
            const auto device_code = ks::derive_device_authentication_code("trustme");
            REQUIRE(device_code.has_value());
            const auto mac = ks::session_response_mac(*device_code, 1u, fixture.client_public, fixture.server_public);
            REQUIRE(mac.has_value());
            return {.session_id = 1u, .server_public_key = fixture.server_public, .mac = *mac};
        }

        /// @brief Seals @p plain as the server would: under the session key, with its own serial number.
        [[nodiscard]] std::vector<std::uint8_t> server_wrapper(const handshake_fixture& fixture, const std::uint64_t sequence,
                                                               const cspan_uint8_t plain, const std::uint16_t session_id = 1u)
        {
            const auto key = ks::derive_session_key(fixture.client_private, fixture.server_public);
            REQUIRE(key.has_value());
            std::vector<std::uint8_t> wire(kn::frame::max_datagram_size, 0u);
            const ks::wrapper_fields fields {.session_id = session_id,
                                             .sequence = ks::encode_sequence(sequence),
                                             .serial_number = server_serial,
                                             .message_tag = ks::tunnelling_message_tag};
            const auto size = ks::seal_wrapper(wire, *key, fields, plain);
            REQUIRE(size.has_value());
            wire.resize(*size);
            return wire;
        }

        /// @brief Seals @p plain as @ref server_wrapper does, but without refusing what may not be wrapped: a frame no
        ///        well-behaved peer sends, built by hand to show the session refuses it too.
        [[nodiscard]] std::vector<std::uint8_t> raw_server_wrapper(const handshake_fixture& fixture, const std::uint64_t sequence,
                                                                   const cspan_uint8_t plain)
        {
            const auto key = ks::derive_session_key(fixture.client_private, fixture.server_public);
            REQUIRE(key.has_value());
            const auto total = ks::wrapper_overhead + plain.size();
            std::vector<std::uint8_t> wire(total, 0u);
            const auto sequence_octets = ks::encode_sequence(sequence);
            // The header and session id 1, which the MAC covers, then the sequence, serial number, message tag and payload.
            const std::array<std::uint8_t, 8u> prefix {
                0x06u, 0x10u, 0x09u, 0x50u, static_cast<std::uint8_t>(total >> 8u), static_cast<std::uint8_t>(total & 0xFFu), 0x00u, 0x01u};
            std::ranges::copy(prefix, wire.begin());
            std::ranges::copy(sequence_octets, wire.begin() + 8u);
            std::ranges::copy(server_serial, wire.begin() + 14u);
            std::ranges::copy(ks::tunnelling_message_tag, wire.begin() + 20u);
            const auto payload = std::span {wire}.subspan(22u, plain.size());
            std::ranges::copy(plain, payload.begin());
            const auto mac = kd::seal(
                kd::evp_backend(), *key,
                kd::wrapper_block_0(sequence_octets, server_serial, ks::tunnelling_message_tag, static_cast<std::uint16_t>(plain.size())),
                kd::wrapper_counter_0(sequence_octets, server_serial, ks::tunnelling_message_tag), std::span {wire}.first(8u), payload);
            REQUIRE(mac.has_value());
            std::ranges::copy(*mac, wire.end() - static_cast<std::ptrdiff_t>(ks::mac_size));
            return wire;
        }

        [[nodiscard]] std::vector<std::uint8_t> status(const ks::session_status value)
        {
            std::vector<std::uint8_t> wire(ks::session_status_size, 0u);
            REQUIRE(ks::encode_session_status_packet(wire, {value}).has_value());
            return wire;
        }

        [[nodiscard]] std::vector<std::uint8_t> tunnelling_request()
        {
            std::vector<std::uint8_t> wire(sample_tunnelling_packet_size, 0u);
            REQUIRE(kn::frame::encode_tunnelling_request_packet(wire, 1u, 0u, sample_cemi).has_value());
            return wire;
        }

        /// @brief Opens a received wire through @p session.
        [[nodiscard]] ks::opened_frame_result_t deliver(ks::client_session& session, const std::vector<std::uint8_t>& wire, buffer_t& plain,
                                                        const std::uint64_t now_ms)
        {
            const auto wrapper = ks::decode_secure_wrapper_packet(wire);
            REQUIRE(wrapper.has_value());
            return session.open(*wrapper, plain, now_ms);
        }

        /// @brief Runs the whole handshake at @p now_ms, leaving @p session established.
        void establish(ks::client_session& session, const handshake_fixture& fixture, const std::uint64_t now_ms)
        {
            buffer_t buffer {};
            REQUIRE(session.begin(buffer, kn::hpai {{}, 0x02u}, now_ms).has_value());
            REQUIRE(session.on_session_response(response(fixture), buffer, now_ms).has_value());
            REQUIRE(deliver(session, server_wrapper(fixture, 0u, status(ks::session_status::authentication_success)), buffer, now_ms)
                        .has_value());
            REQUIRE(session.established());
        }
    }

    TEST_CASE("knx secure client session completes xknx's handshake", "[knx][secure][session][unit]")
    {
        const detail::handshake_fixture fixture {};
        detail::fixed_key_entropy entropy {fixture};
        ks::client_session session {detail::credentials(), entropy};
        detail::buffer_t wire {};

        const auto request_size = session.begin(wire, kn::hpai {{}, 0x02u}, 0u);
        REQUIRE(request_size.has_value());
        const auto request = ks::decode_session_request_packet(std::span {wire}.first(*request_size));
        REQUIRE(request.has_value());
        CHECK(request->client_public_key == fixture.client_public);
        CHECK(session.phase() == ks::client_session_phase::requested);

        // SESSION_AUTHENTICATE goes out as the session's first wrapper, and carries xknx's MAC.
        const auto authenticate_size = session.on_session_response(detail::response(fixture), wire, 0u);
        REQUIRE(authenticate_size.has_value());
        const auto wrapper = ks::decode_secure_wrapper_packet(std::span {wire}.first(*authenticate_size));
        REQUIRE(wrapper.has_value());
        CHECK(wrapper->session_id == 1u);
        CHECK(ks::decode_sequence(wrapper->sequence) == 0u);
        CHECK(wrapper->serial_number == detail::client_serial);
        CHECK(wrapper->message_tag == ks::tunnelling_message_tag);
        const auto key = ks::derive_session_key(fixture.client_private, fixture.server_public);
        REQUIRE(key.has_value());
        detail::buffer_t plain {};
        const auto plain_size = ks::open_wrapper(plain, *key, *wrapper);
        REQUIRE(plain_size.has_value());
        CHECK(std::ranges::equal(std::span {plain}.first(*plain_size),
                                 sv::hex("06 10 09 53 00 18 00 01 1f 1d 59 ea 9f 12 a1 52 e5 d9 72 7f 08 46 2c de")));
        CHECK(session.phase() == ks::client_session_phase::authenticating);

        const auto success = detail::deliver(
            session, detail::server_wrapper(fixture, 0u, detail::status(ks::session_status::authentication_success)), plain, 0u);
        REQUIRE(success.has_value());
        CHECK(!success->for_tunnel);
        CHECK(session.established());
        CHECK(session.counters().sessions_opened == 1u);

        // Tunnel traffic follows under the next sequence number.
        const auto sealed = session.seal(detail::tunnelling_request(), wire, 1u);
        REQUIRE(sealed.has_value());
        const auto next = ks::decode_secure_wrapper_packet(std::span {wire}.first(*sealed));
        REQUIRE(next.has_value());
        CHECK(ks::decode_sequence(next->sequence) == 1u);
    }

    TEST_CASE("knx secure client session stops before authenticating when the response does not verify", "[knx][secure][session][unit]")
    {
        const detail::handshake_fixture fixture {};
        detail::fixed_key_entropy entropy {fixture};
        detail::buffer_t wire {};

        ks::client_session session {detail::credentials(), entropy};
        REQUIRE(session.begin(wire, kn::hpai {{}, 0x02u}, 0u).has_value());
        auto forged = detail::response(fixture);
        forged.mac[0u] ^= 0x01u;
        wire.fill(0xEEu);
        CHECK(detail::error_of(session.on_session_response(forged, wire, 0u)) == make_error_code(error::secure_authentication_failed));
        CHECK(session.phase() == ks::client_session_phase::closed);
        CHECK(session.counters().authentication_failures == 1u);
        // Nothing was written for sending: no SESSION_AUTHENTICATE follows a response that did not verify.
        CHECK(std::ranges::all_of(wire, [](const std::uint8_t octet) noexcept { return octet == 0xEEu; }));

        // Session id zero is routing's, and is refused whatever the MAC.
        ks::client_session zero {detail::credentials(), entropy};
        REQUIRE(zero.begin(wire, kn::hpai {{}, 0x02u}, 0u).has_value());
        auto routing_id = detail::response(fixture);
        routing_id.session_id = 0u;
        CHECK(detail::error_of(zero.on_session_response(routing_id, wire, 0u)) == make_error_code(error::secure_session_rejected));

        // The named opt-out takes the response on trust.
        ks::client_session trusting {detail::credentials(true), entropy};
        REQUIRE(trusting.begin(wire, kn::hpai {{}, 0x02u}, 0u).has_value());
        CHECK(trusting.on_session_response(forged, wire, 0u).has_value());
        CHECK(trusting.phase() == ks::client_session_phase::authenticating);
    }

    TEST_CASE("knx secure client session reports a refused authentication", "[knx][secure][session][unit]")
    {
        const detail::handshake_fixture fixture {};
        detail::fixed_key_entropy entropy {fixture};
        detail::buffer_t wire {};

        ks::client_session session {detail::credentials(), entropy};
        REQUIRE(session.begin(wire, kn::hpai {{}, 0x02u}, 0u).has_value());
        REQUIRE(session.on_session_response(detail::response(fixture), wire, 0u).has_value());
        const auto refused = detail::deliver(
            session, detail::server_wrapper(fixture, 0u, detail::status(ks::session_status::authentication_failed)), wire, 0u);
        CHECK(detail::error_of(refused) == make_error_code(error::secure_session_rejected));
        CHECK(session.phase() == ks::client_session_phase::closed);

        // A server may refuse in the clear before any key exists; once one does, a clear status is not believed.
        ks::client_session early {detail::credentials(), entropy};
        REQUIRE(early.begin(wire, kn::hpai {{}, 0x02u}, 0u).has_value());
        CHECK(detail::error_of(early.on_unwrapped_status({ks::session_status::unauthenticated})) ==
              make_error_code(error::secure_session_rejected));
        ks::client_session established {detail::credentials(), entropy};
        detail::establish(established, fixture, 0u);
        CHECK(detail::error_of(established.on_unwrapped_status({ks::session_status::close})) ==
              make_error_code(error::secure_frame_required));
        CHECK(established.established());
        CHECK(established.counters().unencrypted_refused == 1u);
    }

    TEST_CASE("knx secure client session refuses bad wrappers and leaves its state alone", "[knx][secure][session][unit]")
    {
        const detail::handshake_fixture fixture {};
        detail::fixed_key_entropy entropy {fixture};
        ks::client_session session {detail::credentials(), entropy};
        detail::establish(session, fixture, 0u);
        detail::buffer_t plain {};
        const auto request = detail::tunnelling_request();

        const auto fifth = detail::server_wrapper(fixture, 5u, request);
        const auto delivered = detail::deliver(session, fifth, plain, 1u);
        REQUIRE(delivered.has_value());
        CHECK(delivered->for_tunnel);
        CHECK(std::ranges::equal(std::span {plain}.first(delivered->size), request));

        // The same wrapper again, and an older one, are replays.
        CHECK(detail::error_of(detail::deliver(session, fifth, plain, 2u)) == make_error_code(error::secure_replay));
        CHECK(detail::error_of(detail::deliver(session, detail::server_wrapper(fixture, 3u, request), plain, 2u)) ==
              make_error_code(error::secure_replay));
        CHECK(session.counters().replays == 2u);

        // Another session's id, and a changed octet, do not authenticate.
        CHECK(detail::error_of(detail::deliver(session, detail::server_wrapper(fixture, 6u, request, 2u), plain, 2u)) ==
              make_error_code(error::secure_authentication_failed));
        auto tampered = detail::server_wrapper(fixture, 6u, request);
        tampered[25u] ^= 0x01u;
        CHECK(detail::error_of(detail::deliver(session, tampered, plain, 2u)) == make_error_code(error::secure_authentication_failed));
        CHECK(session.counters().authentication_failures == 2u);

        // A wrapper inside a wrapper authenticates, and is still refused.
        CHECK(detail::error_of(detail::deliver(session, detail::raw_server_wrapper(fixture, 6u, fifth), plain, 2u)) ==
              make_error_code(error::unsupported_service));
        CHECK(session.counters().refused_services == 1u);

        // A wrapper too short to read never reaches the session; its owner counts it.
        const std::vector<std::uint8_t> truncated(fifth.begin(), fifth.end() - 1);
        CHECK(!ks::decode_secure_wrapper_packet(truncated).has_value());
        session.note_unauthenticated();
        CHECK(session.counters().authentication_failures == 3u);

        // None of that moved the session: sequence 6, which every refused wrapper carried, is still accepted.
        CHECK(session.established());
        CHECK(detail::deliver(session, detail::server_wrapper(fixture, 6u, request), plain, 3u).has_value());
    }

    TEST_CASE("knx secure client session keeps alive and times out by the clock", "[knx][secure][session][unit]")
    {
        const detail::handshake_fixture fixture {};
        detail::fixed_key_entropy entropy {fixture};
        ks::client_session session {detail::credentials(), entropy};
        detail::establish(session, fixture, 0u);
        detail::buffer_t wire {};

        // A keep-alive falls due 50 s after the last wrapper sent, and sending a telegram at 40 s moves that to 90 s.
        CHECK(!session.keep_alive_due(49'999u));
        CHECK(session.keep_alive_due(50'000u));
        REQUIRE(session.seal(detail::tunnelling_request(), wire, 40'000u).has_value());
        CHECK(!session.keep_alive_due(89'999u));
        CHECK(session.keep_alive_due(90'000u));

        const auto keep_alive = session.prepare_keep_alive(wire, 90'000u);
        REQUIRE(keep_alive.has_value());
        const auto wrapper = ks::decode_secure_wrapper_packet(std::span {wire}.first(*keep_alive));
        REQUIRE(wrapper.has_value());
        const auto key = ks::derive_session_key(fixture.client_private, fixture.server_public);
        REQUIRE(key.has_value());
        detail::buffer_t plain {};
        const auto plain_size = ks::open_wrapper(plain, *key, *wrapper);
        REQUIRE(plain_size.has_value());
        const auto sent = ks::decode_session_status_packet(std::span {plain}.first(*plain_size));
        REQUIRE(sent.has_value());
        CHECK(sent->status == ks::session_status::keepalive);
        CHECK(!session.keep_alive_due(90'000u));

        // The session ends 60 s after its last traffic, in either direction.
        CHECK(session.check_timeout(149'999u).has_value());
        CHECK(detail::error_of(session.check_timeout(150'000u)) == make_error_code(error::secure_session_closed));
        CHECK(session.phase() == ks::client_session_phase::closed);
        CHECK(session.counters().sessions_timed_out == 1u);
        CHECK(!session.keep_alive_due(500'000u));
    }

    TEST_CASE("knx secure client session ends on the server's close and begins again with a fresh key", "[knx][secure][session][unit]")
    {
        const detail::handshake_fixture fixture {};
        detail::fixed_key_entropy entropy {fixture};
        ks::client_session session {detail::credentials(), entropy};
        detail::establish(session, fixture, 0u);
        detail::buffer_t wire {};

        const auto closed =
            detail::deliver(session, detail::server_wrapper(fixture, 1u, detail::status(ks::session_status::close)), wire, 1u);
        CHECK(detail::error_of(closed) == make_error_code(error::secure_session_closed));
        CHECK(session.phase() == ks::client_session_phase::closed);
        CHECK(session.counters().sessions_closed == 1u);
        CHECK(detail::error_of(session.seal(detail::tunnelling_request(), wire, 1u)) == make_error_code(error::secure_session_closed));

        // Beginning again draws a new key pair, and the new session's first wrapper is sequence zero once more.
        REQUIRE(session.begin(wire, kn::hpai {{}, 0x02u}, 2u).has_value());
        CHECK(entropy.pairs == 2u);
        const auto authenticate = session.on_session_response(detail::response(fixture), wire, 2u);
        REQUIRE(authenticate.has_value());
        const auto wrapper = ks::decode_secure_wrapper_packet(std::span {wire}.first(*authenticate));
        REQUIRE(wrapper.has_value());
        CHECK(ks::decode_sequence(wrapper->sequence) == 0u);

        // A close this side sends is sealed like any status, and ends the session.
        REQUIRE(detail::deliver(session, detail::server_wrapper(fixture, 0u, detail::status(ks::session_status::authentication_success)),
                                wire, 2u)
                    .has_value());
        REQUIRE(session.prepare_close(wire, 3u).has_value());
        CHECK(session.phase() == ks::client_session_phase::closed);
        CHECK(session.counters().sessions_closed == 2u);
    }
}
