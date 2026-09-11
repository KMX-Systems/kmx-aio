/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/error.hpp>
#include <kmx/aio/knx/secure/detail/wrapper_crypto.hpp>
#include <kmx/aio/knx/secure/timer_notify.hpp>
#include <kmx/aio/test/knx/secure_vectors.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace kmx::aio::test::knx::secure::timer_notify_test
{
    namespace kn = kmx::aio::knx;
    namespace ks = kmx::aio::knx::secure;
    namespace kd = kmx::aio::knx::secure::detail;
    namespace sv = kmx::aio::test::knx::secure_vectors;
    using kn::error;
    using kn::make_error_code;

    namespace detail
    {
        /// @brief Verifies a notify after altering one field.
        /// @param alteration 0 timer value, 1 serial number, 2 message tag, 3 MAC.
        [[nodiscard]] bool refused_after(const sv::vector_row& row, const int alteration) noexcept(false)
        {
            auto value = ks::decode_timer_notify_packet(row.wire);
            REQUIRE(value.has_value());
            value->timer_value[5u] ^= (alteration == 0) ? 0x01u : 0x00u;
            value->serial_number[0u] ^= (alteration == 1) ? 0x80u : 0x00u;
            value->message_tag[0u] ^= (alteration == 2) ? 0x01u : 0x00u;
            value->mac[15u] ^= (alteration == 3) ? 0x01u : 0x00u;
            const auto verified = ks::verify_timer_notify(sv::key(row.key), *value);
            return !verified.has_value() && (verified.error() == make_error_code(error::secure_authentication_failed));
        }
    } // namespace detail

    TEST_CASE("knx secure timer notify codec round-trips the xknx frame", "[knx][secure][routing][unit]")
    {
        // xknx test/knxip_tests/timer_notify_test.py.
        const auto raw =
            sv::hex("06 10 09 55 00 24 c0 c1 c2 c3 c4 c5 00 fa 12 34 56 78 af fe 72 12 a0 3a aa e4 9d a8 56 89 77 4c 1d 2b 4d a4");
        const auto decoded = ks::decode_timer_notify_packet(raw);
        REQUIRE(decoded.has_value());
        CHECK(ks::decode_sequence(decoded->timer_value) == 211938428830917u);
        CHECK(decoded->serial_number == sv::fixed<6u>("00 fa 12 34 56 78"));
        CHECK(decoded->message_tag == sv::fixed<2u>("af fe"));
        CHECK(decoded->mac == sv::fixed<16u>("72 12 a0 3a aa e4 9d a8 56 89 77 4c 1d 2b 4d a4"));

        std::array<std::uint8_t, ks::timer_notify_size> encoded {};
        REQUIRE(ks::encode_timer_notify_packet(encoded, *decoded).has_value());
        CHECK(std::ranges::equal(encoded, raw));
        CHECK(ks::encode_timer_notify_packet(std::span {encoded}.first(ks::timer_notify_size - 1u), *decoded).error() ==
              make_error_code(error::invalid_length));
    }

    TEST_CASE("knx secure timer notify matches the notifications xknx builds", "[knx][secure][routing][unit]")
    {
        const auto rows = sv::rows("timer_notify");
        REQUIRE(rows.size() == 4u);
        for (const auto& row: rows)
        {
            INFO("wire=" << row.wire_text);
            const auto key = sv::key(row.key);
            const auto made = ks::make_timer_notify(key, ks::decode_sequence(sv::fixed<6u>(row.timer_value)),
                                                    sv::fixed<6u>(row.serial_number), sv::fixed<2u>(row.message_tag));
            REQUIRE(made.has_value());
            std::array<std::uint8_t, ks::timer_notify_size> encoded {};
            REQUIRE(ks::encode_timer_notify_packet(encoded, *made).has_value());
            CHECK(std::ranges::equal(encoded, row.wire));

            const auto decoded = ks::decode_timer_notify_packet(row.wire);
            REQUIRE(decoded.has_value());
            CHECK(ks::verify_timer_notify(key, *decoded).has_value());
        }
    }

    TEST_CASE("knx secure timer notify refuses a notification altered in any field", "[knx][secure][routing][unit]")
    {
        const auto row = sv::rows("timer_notify").at(1u);
        for (int alteration {}; alteration < 4; ++alteration)
        {
            INFO("alteration=" << alteration);
            CHECK(detail::refused_after(row, alteration));
        }
        const auto decoded = ks::decode_timer_notify_packet(row.wire);
        REQUIRE(decoded.has_value());
        CHECK(ks::verify_timer_notify(sv::key("00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f"), *decoded).error() ==
              make_error_code(error::secure_authentication_failed));
    }

    TEST_CASE("knx secure timer notify refuses malformed notifications and values", "[knx][secure][routing][unit]")
    {
        const auto wire = sv::rows("timer_notify").front().wire;
        CHECK(ks::decode_timer_notify_packet(std::span {wire}.first(wire.size() - 1u)).error() == make_error_code(error::malformed_frame));
        auto longer = wire;
        longer.push_back(0u);
        longer[5u] = static_cast<std::uint8_t>(longer.size());
        CHECK(ks::decode_timer_notify_packet(longer).error() == make_error_code(error::malformed_frame));
        auto wrapper = wire;
        wrapper[3u] = 0x50u;
        CHECK(ks::decode_timer_notify_packet(wrapper).error() == make_error_code(error::unsupported_service));

        const auto key = sv::key("96f034fccf510760cbd63da0f70d4a9d");
        CHECK(ks::make_timer_notify(key, ks::max_sequence + 1u, {}, {}).error() == make_error_code(error::invalid_configuration));
        CHECK(ks::make_timer_notify(key, ks::max_sequence, {}, {}).has_value());

        const auto failing = sv::failing_backend();
        CHECK(kd::basic_make_timer_notify(failing, key, 0u, {}, {}).error() == make_error_code(error::crypto_failure));
        const auto decoded = ks::decode_timer_notify_packet(wire);
        REQUIRE(decoded.has_value());
        CHECK(kd::basic_verify_timer_notify(failing, key, *decoded).error() == make_error_code(error::crypto_failure));
    }
}
