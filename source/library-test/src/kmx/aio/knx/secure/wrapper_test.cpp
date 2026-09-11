/// @file src/kmx/aio/knx/secure/wrapper_test.cpp
/// @brief Unit tests for the KNX IP Secure wrapper: sealing and opening against AN159 and xknx, and refused frames.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/wrapper.hpp>
#ifndef PCH
    #include <kmx/aio/knx/datagram.hpp>
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/secure/detail/wrapper_crypto.hpp>
    #include <kmx/aio/knx/secure/timer_notify.hpp>
    #include <kmx/aio/test/knx/secure_vectors.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <algorithm>
    #include <array>
    #include <cstdint>
    #include <span>
    #include <variant>
#endif

namespace kmx::aio::test::knx::secure::wrapper_test
{
    namespace kn = kmx::aio::knx;
    namespace ks = kmx::aio::knx::secure;
    namespace kd = kmx::aio::knx::secure::detail;
    namespace sv = kmx::aio::test::knx::secure_vectors;
    using kn::error;
    using kn::make_error_code;

    namespace detail
    {
        /// @brief A buffer as large as any datagram this build handles.
        using datagram_buffer_t = std::array<std::uint8_t, kn::frame::max_datagram_size>;

        /// @brief The KNX AN159 routing example: its key, fields and plain ROUTING_INDICATION.
        struct an159_example
        {
            ks::secret_key key {sv::key("00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f")};
            ks::wrapper_fields fields {0u, sv::fixed<6u>("c0 c1 c2 c3 c4 c5"), sv::fixed<6u>("00 fa 12 34 56 78"), sv::fixed<2u>("af fe")};
            sv::octets_t plain {sv::hex("06 10 05 30 00 11 29 00 bc d0 11 59 0a de 01 00 81")};
            sv::octets_t wire {sv::hex("06 10 09 50 00 37 00 00 c0 c1 c2 c3 c4 c5 00 fa 12 34 56 78 af fe"
                                       "b7 ee 7e 8a 1c 2f 7b ba be c7 75 fd 6e 10 d0 bc 4b"
                                       "72 12 a0 3a aa e4 9d a8 56 89 77 4c 1d 2b 4d a4")};
        };

        [[nodiscard]] bool refused_with(const expected_size_t& result, const error expected) noexcept
        {
            return !result.has_value() && (result.error() == make_error_code(expected));
        }

        /// @brief Opens the AN159 wrapper after altering one field, and reports whether it was refused and wiped.
        /// @param alteration 0 session id, 1 sequence, 2 serial number, 3 message tag, 4 ciphertext, 5 MAC.
        [[nodiscard]] bool refused_after(const int alteration) noexcept(false)
        {
            an159_example example {};
            auto wire = example.wire;
            wire[30u] ^= (alteration == 4) ? 0x01u : 0x00u;
            auto value = ks::decode_wrapper_packet(wire);
            REQUIRE(value.has_value());
            value->session_id ^= (alteration == 0) ? 0x0001u : 0x0000u;
            value->sequence[5u] ^= (alteration == 1) ? 0x01u : 0x00u;
            value->serial_number[5u] ^= (alteration == 2) ? 0x01u : 0x00u;
            value->message_tag[1u] ^= (alteration == 3) ? 0x01u : 0x00u;
            value->mac[3u] ^= (alteration == 5) ? 0x01u : 0x00u;

            datagram_buffer_t plain {};
            const auto opened = ks::open_wrapper(plain, example.key, *value);
            return refused_with(opened, error::secure_authentication_failed) && sv::all_zero(plain);
        }
    }

    TEST_CASE("knx secure wrapper seals and opens the AN159 routing example", "[knx][secure][routing][unit]")
    {
        detail::an159_example example {};
        detail::datagram_buffer_t wire {};
        const auto sealed = ks::seal_wrapper(wire, example.key, example.fields, example.plain);
        REQUIRE(sealed.has_value());
        CHECK(std::ranges::equal(std::span {wire}.first(*sealed), example.wire));

        const auto decoded = ks::decode_wrapper_packet(example.wire);
        REQUIRE(decoded.has_value());
        CHECK(decoded->session_id == 0u);
        CHECK(decoded->sequence == example.fields.sequence);
        CHECK(decoded->serial_number == example.fields.serial_number);
        CHECK(decoded->message_tag == example.fields.message_tag);
        CHECK(decoded->encrypted_frame.size() == example.plain.size());

        detail::datagram_buffer_t plain {};
        const auto opened = ks::open_wrapper(plain, example.key, *decoded);
        REQUIRE(opened.has_value());
        CHECK(std::ranges::equal(std::span {plain}.first(*opened), example.plain));
    }

    TEST_CASE("knx secure wrapper matches the wrappers xknx builds", "[knx][secure][routing][unit]")
    {
        const auto rows = sv::rows("wrapper");
        REQUIRE(rows.size() == 4u);
        for (const auto& row: rows)
        {
            INFO("wire=" << row.wire_text);
            const auto key = sv::key(row.key);
            const ks::wrapper_fields fields {0u, sv::fixed<6u>(row.timer_value), sv::fixed<6u>(row.serial_number),
                                             sv::fixed<2u>(row.message_tag)};
            detail::datagram_buffer_t wire {};
            const auto sealed = ks::seal_wrapper(wire, key, fields, row.plain);
            REQUIRE(sealed.has_value());
            CHECK(std::ranges::equal(std::span {wire}.first(*sealed), row.wire));

            const auto decoded = ks::decode_wrapper_packet(row.wire);
            REQUIRE(decoded.has_value());
            detail::datagram_buffer_t plain {};
            const auto opened = ks::open_wrapper(plain, key, *decoded);
            REQUIRE(opened.has_value());
            CHECK(std::ranges::equal(std::span {plain}.first(*opened), row.plain));
        }
    }

    TEST_CASE("knx secure wrapper codec round-trips the xknx session frame", "[knx][secure][routing][unit]")
    {
        // xknx test/knxip_tests/secure_wrapper_test.py: a SESSION_AUTHENTICATE wrapped on session 1.
        const auto raw = sv::hex("06 10 09 50 00 3e 00 01 00 00 00 00 00 00 00 fa 12 34 56 78 af fe"
                                 "79 15 a4 f3 6e 6e 42 08 d2 8b 4a 20 7d 8f 35 c0 d1 38 c2 6a 7b 5e 71 69"
                                 "52 db a8 e7 e4 bd 80 bd 7d 86 8a 3a e7 87 49 de");
        const auto decoded = ks::decode_wrapper_packet(raw);
        REQUIRE(decoded.has_value());
        CHECK(decoded->session_id == 1u);
        CHECK(decoded->sequence == ks::sequence_information_t {});
        CHECK(decoded->encrypted_frame.size() == 24u);
        CHECK(decoded->mac == sv::fixed<16u>("52 db a8 e7 e4 bd 80 bd 7d 86 8a 3a e7 87 49 de"));

        detail::datagram_buffer_t encoded {};
        REQUIRE(ks::encode_wrapper_packet(encoded, *decoded).has_value());
        CHECK(std::ranges::equal(std::span {encoded}.first(raw.size()), raw));
        CHECK(!ks::encode_wrapper_packet(std::span {encoded}.first(raw.size() - 1u), *decoded).has_value());
    }

    TEST_CASE("knx secure wrapper refuses a wrapper altered in any field", "[knx][secure][routing][unit]")
    {
        for (int alteration {}; alteration < 6; ++alteration)
        {
            INFO("alteration=" << alteration);
            CHECK(detail::refused_after(alteration));
        }

        detail::an159_example example {};
        const auto decoded = ks::decode_wrapper_packet(example.wire);
        REQUIRE(decoded.has_value());
        detail::datagram_buffer_t plain {};
        const auto wrong_key = ks::open_wrapper(plain, sv::key("96f034fccf510760cbd63da0f70d4a9d"), *decoded);
        CHECK(detail::refused_with(wrong_key, error::secure_authentication_failed));
        CHECK(sv::all_zero(plain));
    }

    TEST_CASE("knx secure wrapper decoding refuses what is not a whole wrapper", "[knx][secure][routing][unit]")
    {
        detail::an159_example example {};
        const auto timer_notify = sv::rows("timer_notify").front().wire;
        CHECK(ks::decode_wrapper_packet(timer_notify).error() == make_error_code(error::unsupported_service));
        CHECK(ks::decode_wrapper_packet(std::span {example.wire}.first(example.wire.size() - 1u)).error() ==
              make_error_code(error::malformed_frame));

        // A wrapper with no room for a KNXnet/IP header inside: 43 octets, declared as such.
        auto short_wire = sv::octets_t(example.wire.begin(), example.wire.begin() + 43);
        short_wire[5u] = 43u;
        CHECK(ks::decode_wrapper_packet(short_wire).error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx secure wrapper refuses datagrams that may not be wrapped", "[knx][secure][routing][unit]")
    {
        detail::an159_example example {};
        detail::datagram_buffer_t wire {};
        CHECK(detail::refused_with(ks::seal_wrapper(wire, example.key, example.fields, example.wire), error::unsupported_service));
        CHECK(detail::refused_with(ks::seal_wrapper(wire, example.key, example.fields, sv::hex("06 10 07 40 00 06")),
                                   error::unsupported_service));
        CHECK(detail::refused_with(ks::seal_wrapper(wire, example.key, example.fields, sv::hex("06 10 07 43 00 06")),
                                   error::unsupported_service));
        CHECK(ks::check_wrapped_frame(sv::hex("06 10 02 01 00 06")).has_value());

        auto wrong_length = example.plain;
        wrong_length[5u] = 0x20u;
        CHECK(detail::refused_with(ks::seal_wrapper(wire, example.key, example.fields, wrong_length), error::malformed_frame));
        CHECK(detail::refused_with(
            ks::seal_wrapper(std::span {wire}.first(example.wire.size() - 1u), example.key, example.fields, example.plain),
            error::invalid_length));

        // One octet more than a wrapper of the buffered datagram size can carry.
        sv::octets_t oversized(ks::max_wrapped_frame_size + 1u, 0u);
        std::ranges::copy(sv::hex("06 10 05 30"), oversized.begin());
        oversized[4u] = static_cast<std::uint8_t>(oversized.size() >> 8u);
        oversized[5u] = static_cast<std::uint8_t>(oversized.size() & 0xFFu);
        CHECK(detail::refused_with(ks::seal_wrapper(wire, example.key, example.fields, oversized), error::invalid_length));
    }

    TEST_CASE("knx secure wrapper reports a backend failure and wipes what it wrote", "[knx][secure][routing][unit]")
    {
        detail::an159_example example {};
        const auto failing = sv::failing_backend();
        detail::datagram_buffer_t wire {};
        wire.fill(0xAAu);
        CHECK(detail::refused_with(kd::basic_seal_wrapper({failing, example.key}, wire, example.fields, example.plain),
                                   error::crypto_failure));
        CHECK(sv::all_zero(std::span {wire}.first(example.wire.size())));

        const auto decoded = ks::decode_wrapper_packet(example.wire);
        REQUIRE(decoded.has_value());
        detail::datagram_buffer_t plain {};
        plain.fill(0xAAu);
        CHECK(detail::refused_with(kd::basic_open_wrapper({failing, example.key}, plain, *decoded), error::crypto_failure));
        CHECK(sv::all_zero(std::span {plain}.first(example.plain.size())));
    }

    TEST_CASE("knx datagram dispatch decodes wrappers and timer notifications", "[knx][secure][routing][datagram][unit]")
    {
        detail::an159_example example {};
        const auto wrapper = kn::decode_datagram(example.wire);
        REQUIRE(wrapper.has_value());
        CHECK(wrapper->service_type == ks::wrapper_service);
        const auto* frame = std::get_if<ks::wrapper_frame>(&wrapper->payload);
        REQUIRE(frame != nullptr);
        CHECK(frame->serial_number == example.fields.serial_number);
        detail::datagram_buffer_t encoded {};
        REQUIRE(kn::encode_datagram(encoded, *wrapper).has_value());
        CHECK(std::ranges::equal(std::span {encoded}.first(example.wire.size()), example.wire));

        const auto notify_wire = sv::rows("timer_notify").front().wire;
        const auto notify = kn::decode_datagram(notify_wire);
        REQUIRE(notify.has_value());
        CHECK(std::holds_alternative<ks::timer_notify_frame>(notify->payload));
        REQUIRE(kn::encode_datagram(encoded, *notify).has_value());
        CHECK(std::ranges::equal(std::span {encoded}.first(notify_wire.size()), notify_wire));

        // The session services decode like the rest: a SESSION_STATUS reporting authentication success round-trips.
        const auto status_wire = sv::hex("06 10 09 54 00 08 00 00");
        const auto status = kn::decode_datagram(status_wire);
        REQUIRE(status.has_value());
        REQUIRE(std::holds_alternative<ks::session_status_frame>(status->payload));
        CHECK(std::get<ks::session_status_frame>(status->payload).status == ks::session_status::authentication_success);
        REQUIRE(kn::encode_datagram(encoded, *status).has_value());
        CHECK(std::ranges::equal(std::span {encoded}.first(status_wire.size()), status_wire));
    }
}
