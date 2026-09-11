/// @file kmx/aio/knx/secure/session_test.cpp
/// @brief The KNX IP Secure session services - codecs, handshake MACs and the session key - against xknx's vectors.
/// @details The fixture is xknx's: a fixed client key pair, the server's public key, session id 1, the device
/// authentication code "trustme" and the user password "secret". Every MAC, key and wrapper below is an octet string
/// xknx computes for the same inputs.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/datagram.hpp>
#include <kmx/aio/knx/error.hpp>
#include <kmx/aio/knx/secure/session.hpp>
#include <kmx/aio/knx/secure/wrapper.hpp>
#include <kmx/aio/test/knx/secure_vectors.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <variant>

namespace kmx::aio::test::knx::secure::session_test
{
    namespace kn = kmx::aio::knx;
    namespace ks = kmx::aio::knx::secure;
    namespace sv = kmx::aio::test::knx::secure_vectors;
    using kn::error;
    using kn::make_error_code;

    namespace detail
    {
        /// @brief xknx's session fixture.
        struct handshake_fixture
        {
            ks::x25519_private_key client_private {
                sv::fixed<32u>("b8 fa bd 62 66 5d 8b 9e 8a 9d 8b 1f 4b ca 42 c8 c2 78 9a 61 10 f5 0e 9d d7 85 b3 ed e8 83 f3 78")};
            ks::x25519_public_key_t client_public {
                sv::fixed<32u>("0a a2 27 b4 fd 7a 32 31 9b a9 96 0a c0 36 ce 0e 5c 45 07 b5 ae 55 16 1f 10 78 b1 dc fb 3c b6 31")};
            ks::x25519_public_key_t server_public {
                sv::fixed<32u>("bd f0 99 90 99 23 14 3e f0 a5 de 0b 3b e3 68 7b c5 bd 3c f5 f9 e6 f9 01 69 9c d8 70 ec 1f f8 24")};
            std::uint16_t session_id = 1u;
        };

        /// @brief Returns the error a result carries, or no error.
        template <typename Value>
        [[nodiscard]] std::error_code error_of(const std::expected<Value, std::error_code>& result) noexcept
        {
            return result.has_value() ? std::error_code {} : result.error();
        }
    }

    TEST_CASE("knx secure session request round-trips with a TCP control endpoint", "[knx][secure][session][unit]")
    {
        const detail::handshake_fixture fixture {};
        const ks::session_request_frame request {.control_endpoint = kn::hpai {{}, 0x02u}, .client_public_key = fixture.client_public};
        std::array<std::uint8_t, ks::session_request_size> wire {};
        REQUIRE(ks::encode_session_request_packet(wire, request).has_value());
        const auto expected = sv::hex("06 10 09 51 00 2e 08 02 00 00 00 00 00 00"
                                      "0a a2 27 b4 fd 7a 32 31 9b a9 96 0a c0 36 ce 0e 5c 45 07 b5 ae 55 16 1f 10 78 b1 dc fb 3c b6 31");
        CHECK(std::ranges::equal(wire, expected));

        const auto decoded = kn::decode_datagram(wire);
        REQUIRE(decoded.has_value());
        const auto* const frame = std::get_if<ks::session_request_frame>(&decoded->payload);
        REQUIRE(frame != nullptr);
        CHECK(frame->control_endpoint.protocol == 0x02u);
        CHECK(frame->client_public_key == fixture.client_public);

        // A short buffer, an HPAI naming no host protocol, and an HPAI of the wrong length are all refused.
        std::array<std::uint8_t, ks::session_request_size - 1u> short_buffer {};
        CHECK(detail::error_of(ks::encode_session_request_packet(short_buffer, request)) == make_error_code(error::invalid_length));
        auto unknown = request;
        unknown.control_endpoint.protocol = 0x03u;
        CHECK(detail::error_of(ks::encode_session_request_packet(wire, unknown)) == make_error_code(error::unsupported_hpai));
        auto wrong_length = expected;
        wrong_length[6u] = 0x07u;
        CHECK(detail::error_of(ks::decode_session_request_packet(wrong_length)) == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx secure session response carries xknx's MAC, which verifies only against the right key", "[knx][secure][session][unit]")
    {
        const detail::handshake_fixture fixture {};
        const auto device_code = ks::derive_device_authentication_code("trustme");
        REQUIRE(device_code.has_value());
        const auto mac = ks::session_response_mac(*device_code, fixture.session_id, fixture.client_public, fixture.server_public);
        REQUIRE(mac.has_value());
        CHECK(std::ranges::equal(*mac, sv::hex("a9 22 50 5a aa 43 61 63 57 0b d5 49 4c 2d f2 a3")));

        const ks::session_response_frame response {
            .session_id = fixture.session_id, .server_public_key = fixture.server_public, .mac = *mac};
        std::array<std::uint8_t, ks::session_response_size> wire {};
        REQUIRE(ks::encode_session_response_packet(wire, response).has_value());
        CHECK(std::ranges::equal(std::span {wire}.first(8u), sv::hex("06 10 09 52 00 38 00 01")));
        const auto decoded = ks::decode_session_response_packet(wire);
        REQUIRE(decoded.has_value());
        CHECK(decoded->server_public_key == fixture.server_public);
        CHECK(ks::verify_session_response(*device_code, *decoded, fixture.client_public).has_value());

        // Another code, one changed MAC octet, or another client's key all fail the same way.
        const auto other_code = ks::derive_device_authentication_code("trustyou");
        REQUIRE(other_code.has_value());
        const auto refused = make_error_code(error::secure_authentication_failed);
        CHECK(detail::error_of(ks::verify_session_response(*other_code, *decoded, fixture.client_public)) == refused);
        auto altered = *decoded;
        altered.mac[15u] ^= 0x01u;
        CHECK(detail::error_of(ks::verify_session_response(*device_code, altered, fixture.client_public)) == refused);
        auto other_client = fixture.client_public;
        other_client[0u] ^= 0x01u;
        CHECK(detail::error_of(ks::verify_session_response(*device_code, *decoded, other_client)) == refused);
    }

    TEST_CASE("knx secure session authenticate carries xknx's MAC", "[knx][secure][session][unit]")
    {
        const detail::handshake_fixture fixture {};
        const auto user_key = ks::derive_user_password_key("secret");
        REQUIRE(user_key.has_value());
        const auto mac = ks::session_authenticate_mac(*user_key, 1u, fixture.client_public, fixture.server_public);
        REQUIRE(mac.has_value());
        CHECK(std::ranges::equal(*mac, sv::hex("1f 1d 59 ea 9f 12 a1 52 e5 d9 72 7f 08 46 2c de")));

        const ks::session_authenticate_frame authenticate {.user_id = 1u, .mac = *mac};
        std::array<std::uint8_t, ks::session_authenticate_size> wire {};
        REQUIRE(ks::encode_session_authenticate_packet(wire, authenticate).has_value());
        const auto expected = sv::hex("06 10 09 53 00 18 00 01 1f 1d 59 ea 9f 12 a1 52 e5 d9 72 7f 08 46 2c de");
        CHECK(std::ranges::equal(wire, expected));

        const auto decoded = ks::decode_session_authenticate_packet(wire);
        REQUIRE(decoded.has_value());
        CHECK(ks::verify_session_authenticate(*user_key, *decoded, fixture.client_public, fixture.server_public).has_value());
        // The MAC covers the user id: the same MAC for another user does not verify.
        auto other_user = *decoded;
        other_user.user_id = 2u;
        CHECK(detail::error_of(ks::verify_session_authenticate(*user_key, other_user, fixture.client_public, fixture.server_public)) ==
              make_error_code(error::secure_authentication_failed));
        auto reserved = expected;
        reserved[6u] = 0x01u;
        CHECK(detail::error_of(ks::decode_session_authenticate_packet(reserved)) == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx secure session key and first wrappers reproduce xknx's", "[knx][secure][session][unit]")
    {
        const detail::handshake_fixture fixture {};
        const auto session_key = ks::derive_session_key(fixture.client_private, fixture.server_public);
        REQUIRE(session_key.has_value());
        CHECK(std::ranges::equal(session_key->bytes(), sv::hex("28 94 26 c2 91 25 35 ba 98 27 9a 4d 18 43 c4 87")));

        // The client's SESSION_AUTHENTICATE, sealed as the first wrapper of the session.
        const ks::wrapper_fields client_fields {.session_id = fixture.session_id,
                                                .sequence = ks::encode_sequence(0u),
                                                .serial_number = {0x00u, 0xFAu, 0x12u, 0x34u, 0x56u, 0x78u},
                                                .message_tag = {0xAFu, 0xFEu}};
        const auto authenticate = sv::hex("06 10 09 53 00 18 00 01 1f 1d 59 ea 9f 12 a1 52 e5 d9 72 7f 08 46 2c de");
        std::array<std::uint8_t, kn::frame::max_datagram_size> sealed {};
        const auto sealed_size = ks::seal_wrapper(sealed, *session_key, client_fields, authenticate);
        REQUIRE(sealed_size.has_value());
        const auto expected_wire = sv::hex("06 10 09 50 00 3e 00 01 00 00 00 00 00 00 00 fa 12 34 56 78 af fe"
                                           "79 15 a4 f3 6e 6e 42 08 d2 8b 4a 20 7d 8f 35 c0 d1 38 c2 6a 7b 5e 71 69"
                                           "52 db a8 e7 e4 bd 80 bd 7d 86 8a 3a e7 87 49 de");
        CHECK(std::ranges::equal(std::span {sealed}.first(*sealed_size), expected_wire));

        // The server's answer, wrapped under a serial number of its own: authentication succeeded.
        const auto server_wire = sv::hex("06 10 09 50 00 2e 00 01 00 00 00 00 00 00 00 fa aa aa aa aa af fe"
                                         "26 15 6d b5 c7 49 88 8f"
                                         "a3 73 c3 e0 b4 bd e4 49 7c 39 5e 4b 1c 2f 46 a1");
        const auto wrapper = ks::decode_secure_wrapper_packet(server_wire);
        REQUIRE(wrapper.has_value());
        std::array<std::uint8_t, kn::frame::max_datagram_size> opened {};
        const auto opened_size = ks::open_wrapper(opened, *session_key, *wrapper);
        REQUIRE(opened_size.has_value());
        const auto status = ks::decode_session_status_packet(std::span {opened}.first(*opened_size));
        REQUIRE(status.has_value());
        CHECK(status->status == ks::session_status::authentication_success);
    }

    TEST_CASE("knx secure session status encodes every defined status and refuses the rest", "[knx][secure][session][unit]")
    {
        for (std::uint8_t code = 0u; code <= 5u; ++code)
        {
            std::array<std::uint8_t, ks::session_status_size> wire {};
            REQUIRE(ks::encode_session_status_packet(wire, {static_cast<ks::session_status>(code)}).has_value());
            CHECK(wire[6u] == code);
            const auto decoded = ks::decode_session_status_packet(wire);
            REQUIRE(decoded.has_value());
            CHECK(static_cast<std::uint8_t>(decoded->status) == code);
        }

        std::array<std::uint8_t, ks::session_status_size> wire {};
        CHECK(detail::error_of(ks::encode_session_status_packet(wire, {static_cast<ks::session_status>(6u)})) ==
              make_error_code(error::invalid_configuration));
        CHECK(detail::error_of(ks::decode_session_status_packet(sv::hex("06 10 09 54 00 08 06 00"))) ==
              make_error_code(error::malformed_frame));
        CHECK(detail::error_of(ks::decode_session_status_packet(sv::hex("06 10 09 54 00 09 00 00 00"))) ==
              make_error_code(error::malformed_frame));
        // A frame announcing another service is not a status, whatever its length.
        CHECK(detail::error_of(ks::decode_session_status_packet(sv::hex("06 10 09 53 00 08 00 00"))) ==
              make_error_code(error::unsupported_service));
    }
}
