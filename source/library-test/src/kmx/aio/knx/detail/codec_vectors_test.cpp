/// @file src/kmx/aio/knx/detail/codec_vectors_test.cpp
/// @brief KNX codec golden vectors checked at compile time, and decoded back to the values they were built from.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/detail/codec_vectors.hpp>
#ifndef PCH
    #include <catch2/catch_test_macros.hpp>

    #include <cstdint>
#endif

namespace kmx::aio::test::knx::detail::codec_vectors_test
{
    namespace golden = kmx::aio::knx::detail::codec_vectors;

    using namespace kmx::aio::knx;

    // Including the vectors header is the test: every golden encoding and decoding in it is a
    // static_assert, so a wire-format regression fails this translation unit rather than a run. The cases
    // below only record what those assertions cover, so a reader of the test report can see it.

    TEST_CASE("knx codec golden vectors hold at compile time", "[knx][codec][unit]")
    {
        STATIC_CHECK(golden::encode_switch_on() == golden::group_value_write_on);
        STATIC_CHECK(golden::encode_read() == golden::group_value_read);
        STATIC_CHECK(golden::encode_temperature() == golden::group_value_write_temperature);
    }

    TEST_CASE("knx codec golden vectors decode to the values they were built from", "[knx][codec][unit]")
    {
        const auto write = cemi::decode(golden::group_value_write_on);
        REQUIRE(write.has_value());
        CHECK(write->source == golden::source);
        CHECK(write->group_destination() == golden::destination);
        CHECK(write->application_service == apci::group_value_write);
        CHECK(dpt::decode<1u>(*write, golden::group_value_write_on).value());

        const auto temperature = cemi::decode(golden::group_value_write_temperature);
        REQUIRE(temperature.has_value());
        CHECK(dpt::decode<9u>(*temperature, golden::group_value_write_temperature).value() == 21.5f);
    }
}
