/// @file src/kmx/aio/knx/dpt_test.cpp
/// @brief Unit tests for KNX datapoint type encoding and decoding across every implemented main type.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/dpt.hpp>
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/dpt/descriptor.hpp>
    #include <kmx/aio/knx/dpt/payload.hpp>
    #include <kmx/aio/knx/dpt/string_value.hpp>
    #include <kmx/aio/knx/dpt/traits.hpp>
    #include <kmx/aio/knx/dpt/value_view.hpp>
    #include <kmx/aio/knx/error.hpp>

    #include <catch2/catch_approx.hpp>
    #include <catch2/catch_test_macros.hpp>

    #include <array>
    #include <cstdint>
    #include <utility>
    #include <vector>
#endif

namespace kmx::aio::test::knx::dpt_test
{
    using namespace kmx::aio::knx;
    using Catch::Approx;

    namespace detail
    {
        /// @brief Builds the value view a decoder sees for an octet payload.
        /// @param octets The payload octets.
        /// @return The view.
        [[nodiscard]] constexpr dpt::value_view extended(const cspan_uint8_t octets) noexcept
        {
            return {.compacted = false, .compact_value = 0u, .octets = octets};
        }

        /// @brief Builds the value view a decoder sees for a compact payload.
        /// @param value The six-bit value.
        /// @return The view.
        [[nodiscard]] constexpr dpt::value_view compact(const std::uint8_t value) noexcept
        {
            return {.compacted = true, .compact_value = value, .octets = {}};
        }

        /// @brief Encodes a value and hands back the view a decoder would see for it.
        /// @tparam Main The datapoint main type.
        /// @param value The value to encode.
        /// @param storage Storage the returned view can borrow.
        /// @return The view of the encoded value.
        template <std::uint16_t Main>
        [[nodiscard]] dpt::value_view round_trip(const dpt::value_t<Main>& value, dpt::payload& storage) noexcept(false)
        {
            storage = dpt::encode<Main>(value).value();
            return storage.compacted() ? compact(storage.compact_value()) : extended(storage.view());
        }
    }

    TEST_CASE("knx dpt describes every main type it implements", "[knx][dpt][unit]")
    {
        STATIC_CHECK(dpt::describe(1u).value().bit_size == 1u);
        STATIC_CHECK(dpt::describe(1u).value().compact());
        STATIC_CHECK(dpt::describe(9u).value().octet_size() == 2u);
        STATIC_CHECK(dpt::describe(16u).value().octet_size() == 14u);
        STATIC_CHECK(!dpt::describe(5u).value().compact());
        CHECK(dpt::describe(99u).error() == error::unsupported_datapoint);
        CHECK(dpt::describe(1u)->name == "1-bit");
    }

    TEST_CASE("knx dpt 1 rides in the apci octet", "[knx][dpt][unit]")
    {
        const auto on = dpt::encode<1u>(true);
        REQUIRE(on.has_value());
        CHECK(on->compacted());
        CHECK(on->compact_value() == 1u);
        CHECK(dpt::encode<1u>(false)->compact_value() == 0u);

        CHECK(dpt::decode<1u>(detail::compact(1u)).value());
        CHECK(!dpt::decode<1u>(detail::compact(0u)).value());
    }

    TEST_CASE("knx dpt 1 also accepts a value a device sent as a whole octet", "[knx][dpt][unit]")
    {
        const std::array<std::uint8_t, 1u> octet {0x01u};
        CHECK(dpt::decode<1u>(detail::extended(octet)).value());

        const std::array<std::uint8_t, 2u> too_wide {0x00u, 0x01u};
        CHECK(dpt::decode<1u>(detail::extended(too_wide)).error() == error::unsupported_datapoint);
    }

    TEST_CASE("knx dpt 2 and 3 round-trip their control bits", "[knx][dpt][unit]")
    {
        dpt::payload storage {};

        const dpt::controlled_bool controlled {.control = true, .value = false};
        CHECK(dpt::decode<2u>(detail::round_trip<2u>(controlled, storage)).value() == controlled);
        CHECK(dpt::encode<2u>(controlled)->compact_value() == 0x02u);

        const dpt::control_step step {.increase = true, .step_code = 5u};
        CHECK(dpt::decode<3u>(detail::round_trip<3u>(step, storage)).value() == step);
        CHECK(dpt::encode<3u>(step)->compact_value() == 0x0Du);
        CHECK(dpt::encode<3u>(dpt::control_step {.increase = false, .step_code = 8u}).error() == error::value_out_of_range);
    }

    TEST_CASE("knx dpt integer types round-trip across their full range", "[knx][dpt][unit]")
    {
        dpt::payload storage {};

        CHECK(dpt::decode<4u>(detail::round_trip<4u>('K', storage)).value() == 'K');
        CHECK(dpt::decode<5u>(detail::round_trip<5u>(std::uint8_t {255u}, storage)).value() == 255u);
        CHECK(dpt::decode<6u>(detail::round_trip<6u>(std::int8_t {-128}, storage)).value() == -128);
        CHECK(dpt::decode<7u>(detail::round_trip<7u>(std::uint16_t {65535u}, storage)).value() == 65535u);
        CHECK(dpt::decode<8u>(detail::round_trip<8u>(std::int16_t {-32768}, storage)).value() == -32768);
        CHECK(dpt::decode<12u>(detail::round_trip<12u>(std::uint32_t {4294967295u}, storage)).value() == 4294967295u);
        CHECK(dpt::decode<13u>(detail::round_trip<13u>(std::int32_t {-2147483648}, storage)).value() == -2147483648);
        CHECK(dpt::decode<20u>(detail::round_trip<20u>(std::uint8_t {3u}, storage)).value() == 3u);
    }

    TEST_CASE("knx dpt integer types are big-endian on the wire", "[knx][dpt][unit]")
    {
        const auto value = dpt::encode<7u>(std::uint16_t {0x1234u});
        REQUIRE(value.has_value());
        REQUIRE(value->view().size() == 2u);
        CHECK(value->view()[0u] == 0x12u);
        CHECK(value->view()[1u] == 0x34u);

        const auto wide = dpt::encode<12u>(std::uint32_t {0x01020304u});
        REQUIRE(wide.has_value());
        REQUIRE(wide->view().size() == 4u);
        CHECK(wide->view()[0u] == 0x01u);
        CHECK(wide->view()[3u] == 0x04u);
    }

    TEST_CASE("knx dpt 9 matches the published float encodings", "[knx][dpt][unit]")
    {
        const std::array<std::pair<float, std::uint16_t>, 5u> vectors {{
            {0.0f, 0x0000u},
            {21.5f, 0x0C33u},
            {-5.0f, 0x860Cu},
            {-30.0f, 0x8A24u},
            {670760.96f, 0x7FFFu},
        }};

        for (const auto& [value, word]: vectors)
        {
            const auto encoded = dpt::encode<9u>(value);
            REQUIRE(encoded.has_value());
            REQUIRE(encoded->view().size() == 2u);
            const auto raw = static_cast<std::uint16_t>((encoded->view()[0u] << 8u) | encoded->view()[1u]);
            CHECK(raw == word);

            const std::array<std::uint8_t, 2u> octets {static_cast<std::uint8_t>(word >> 8u), static_cast<std::uint8_t>(word & 0xFFu)};
            CHECK(dpt::decode<9u>(detail::extended(octets)).value() == Approx(value).epsilon(0.001));
        }
    }

    TEST_CASE("knx dpt 9 refuses values it cannot represent", "[knx][dpt][unit]")
    {
        CHECK(dpt::encode<9u>(dpt::traits<9u>::max_value).has_value());
        CHECK(dpt::encode<9u>(dpt::traits<9u>::min_value).has_value());
        CHECK(dpt::encode<9u>(700000.0f).error() == error::value_out_of_range);
        CHECK(dpt::encode<9u>(-700000.0f).error() == error::value_out_of_range);
    }

    TEST_CASE("knx dpt 14 carries an ieee 754 pattern unchanged", "[knx][dpt][unit]")
    {
        dpt::payload storage {};
        CHECK(dpt::decode<14u>(detail::round_trip<14u>(3.14159265f, storage)).value() == 3.14159265f);

        const auto encoded = dpt::encode<14u>(1.0f);
        REQUIRE(encoded.has_value());
        REQUIRE(encoded->view().size() == 4u);
        CHECK(encoded->view()[0u] == 0x3Fu);
        CHECK(encoded->view()[1u] == 0x80u);
    }

    TEST_CASE("knx dpt 5 sub types scale onto the full octet", "[knx][dpt][unit]")
    {
        const auto half = dpt::encode_scaling(50.0);
        REQUIRE(half.has_value());
        CHECK(half->view()[0u] == 128u);
        CHECK(dpt::encode_scaling(0.0)->view()[0u] == 0u);
        CHECK(dpt::encode_scaling(100.0)->view()[0u] == 255u);
        CHECK(dpt::encode_scaling(100.1).error() == error::value_out_of_range);

        const std::array<std::uint8_t, 1u> full {255u};
        CHECK(dpt::decode_scaling(detail::extended(full)).value() == Approx(100.0));

        CHECK(dpt::encode_angle(180.0)->view()[0u] == 128u);
        CHECK(dpt::decode_angle(detail::extended(full)).value() == Approx(360.0));
        CHECK(dpt::encode_angle(361.0).error() == error::value_out_of_range);
    }

    TEST_CASE("knx dpt 10 round-trips a time of day", "[knx][dpt][unit]")
    {
        dpt::payload storage {};
        const dpt::time_of_day noon {.weekday = 3u, .hour = 12u, .minute = 30u, .second = 45u};
        CHECK(dpt::decode<10u>(detail::round_trip<10u>(noon, storage)).value() == noon);

        const auto encoded = dpt::encode<10u>(noon);
        REQUIRE(encoded.has_value());
        CHECK(encoded->view()[0u] == 0x6Cu);
        CHECK(dpt::encode<10u>(dpt::time_of_day {.weekday = 0u, .hour = 24u, .minute = 0u, .second = 0u}).error() ==
              error::value_out_of_range);
    }

    TEST_CASE("knx dpt 11 maps two year digits onto the century", "[knx][dpt][unit]")
    {
        dpt::payload storage {};
        const dpt::date today {.day = 5u, .month = 9u, .year = 2026u};
        CHECK(dpt::decode<11u>(detail::round_trip<11u>(today, storage)).value() == today);

        const dpt::date last_century {.day = 31u, .month = 12u, .year = 1999u};
        CHECK(dpt::decode<11u>(detail::round_trip<11u>(last_century, storage)).value() == last_century);

        CHECK(dpt::encode<11u>(dpt::date {.day = 0u, .month = 1u, .year = 2000u}).error() == error::value_out_of_range);
        CHECK(dpt::encode<11u>(dpt::date {.day = 1u, .month = 13u, .year = 2000u}).error() == error::value_out_of_range);
        CHECK(dpt::encode<11u>(dpt::date {.day = 1u, .month = 1u, .year = 1989u}).error() == error::value_out_of_range);
        CHECK(dpt::encode<11u>(dpt::date {.day = 1u, .month = 1u, .year = 2090u}).error() == error::value_out_of_range);
    }

    TEST_CASE("knx dpt 16 pads a string to fourteen octets", "[knx][dpt][unit]")
    {
        const auto text = dpt::string_value::make("kmx-aio");
        REQUIRE(text.has_value());
        CHECK(text->view() == "kmx-aio");

        const auto encoded = dpt::encode<16u>(*text);
        REQUIRE(encoded.has_value());
        CHECK(encoded->view().size() == dpt::string_value::capacity);
        CHECK(encoded->view()[7u] == 0u);

        CHECK(dpt::decode<16u>(detail::extended(encoded->view())).value().view() == "kmx-aio");
        CHECK(dpt::string_value::make("far too long for a dpt 16").error() == error::payload_too_large);
    }

    TEST_CASE("knx dpt scene types mask the learn bit", "[knx][dpt][unit]")
    {
        dpt::payload storage {};
        CHECK(dpt::decode<17u>(detail::round_trip<17u>(std::uint8_t {17u}, storage)).value() == 17u);
        CHECK(dpt::encode<17u>(std::uint8_t {64u}).error() == error::value_out_of_range);

        const dpt::scene_control store {.learn = true, .scene = 5u};
        CHECK(dpt::decode<18u>(detail::round_trip<18u>(store, storage)).value() == store);
        CHECK(dpt::encode<18u>(store)->view()[0u] == 0x85u);
        CHECK(dpt::encode<18u>(dpt::scene_control {.learn = false, .scene = 64u}).error() == error::value_out_of_range);
    }

    TEST_CASE("knx dpt 232 round-trips a colour", "[knx][dpt][unit]")
    {
        dpt::payload storage {};
        const dpt::rgb colour {.red = 0x12u, .green = 0x34u, .blue = 0x56u};
        CHECK(dpt::decode<232u>(detail::round_trip<232u>(colour, storage)).value() == colour);
        CHECK(dpt::encode<232u>(colour)->view().size() == 3u);
    }

    TEST_CASE("knx dpt decoders reject a payload of the wrong width", "[knx][dpt][unit]")
    {
        const std::array<std::uint8_t, 1u> one {0u};
        const std::array<std::uint8_t, 3u> three {0u, 0u, 0u};

        CHECK(dpt::decode<9u>(detail::extended(one)).error() == error::unsupported_datapoint);
        CHECK(dpt::decode<14u>(detail::extended(three)).error() == error::unsupported_datapoint);
        CHECK(dpt::decode<10u>(detail::extended(one)).error() == error::unsupported_datapoint);
        CHECK(dpt::decode<11u>(detail::compact(0u)).error() == error::unsupported_datapoint);
        CHECK(dpt::decode<16u>(detail::extended(three)).error() == error::unsupported_datapoint);
        CHECK(dpt::decode<232u>(detail::extended(one)).error() == error::unsupported_datapoint);
        CHECK(dpt::decode<5u>(detail::compact(1u)).error() == error::unsupported_datapoint);
    }

    TEST_CASE("knx dpt decoders reject impossible field values", "[knx][dpt][unit]")
    {
        const std::array<std::uint8_t, 3u> bad_hour {0x18u, 0x00u, 0x00u};
        CHECK(dpt::decode<10u>(detail::extended(bad_hour)).error() == error::value_out_of_range);

        const std::array<std::uint8_t, 3u> bad_month {0x01u, 0x0Du, 0x00u};
        CHECK(dpt::decode<11u>(detail::extended(bad_month)).error() == error::value_out_of_range);
    }

    TEST_CASE("knx dpt payload compares by form and content", "[knx][dpt][unit]")
    {
        const std::array<std::uint8_t, 2u> octets {0x01u, 0x02u};
        const auto first = dpt::payload::octets(octets);
        const auto second = dpt::payload::octets(octets);
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());

        CHECK(*first == *second);
        CHECK(!(*first == dpt::payload::compact(1u)));
        CHECK(dpt::payload::octets(std::vector<std::uint8_t>(dpt::payload::capacity + 1u, 0u)).error() == error::payload_too_large);
    }

    TEST_CASE("knx dpt payload hands the cemi encoder a matching apdu", "[knx][dpt][unit]")
    {
        const auto compact = dpt::encode<1u>(true);
        REQUIRE(compact.has_value());
        CHECK(compact->apdu().compacted());
        CHECK(compact->apdu().data_length() == 1u);

        const auto wide = dpt::encode<9u>(21.5f);
        REQUIRE(wide.has_value());
        CHECK(!wide->apdu().compacted());
        CHECK(wide->apdu().data_length() == 3u);
        CHECK(wide->apdu().octets().size() == 2u);
    }
}
