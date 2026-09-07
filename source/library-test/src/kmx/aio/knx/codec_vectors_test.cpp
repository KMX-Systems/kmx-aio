/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/detail/codec_vectors.hpp>

#include <cstdint>

namespace kmx::aio::test::knx::codec_vectors_test
{
    using namespace kmx::aio::knx;

    // Including the vectors header is the test: every golden encoding and decoding in it is a
    // static_assert, so a wire-format regression fails this translation unit rather than a run. The cases
    // below only record what those assertions cover, so a reader of the test report can see it.

    TEST_CASE("knx codec golden vectors hold at compile time", "[knx][codec][unit]")
    {
        STATIC_CHECK(detail::vectors::encode_switch_on() == detail::vectors::group_value_write_on);
        STATIC_CHECK(detail::vectors::encode_read() == detail::vectors::group_value_read);
        STATIC_CHECK(detail::vectors::encode_temperature() == detail::vectors::group_value_write_temperature);
    }

    TEST_CASE("knx codec golden vectors decode to the values they were built from", "[knx][codec][unit]")
    {
        const auto write = cemi::decode(detail::vectors::group_value_write_on);
        REQUIRE(write.has_value());
        CHECK(write->source == detail::vectors::source);
        CHECK(write->group_destination() == detail::vectors::destination);
        CHECK(write->application_service == apci::group_value_write);
        CHECK(dpt::decode<1u>(*write, detail::vectors::group_value_write_on).value());

        const auto temperature = cemi::decode(detail::vectors::group_value_write_temperature);
        REQUIRE(temperature.has_value());
        CHECK(dpt::decode<9u>(*temperature, detail::vectors::group_value_write_temperature).value() == 21.5f);
    }
}
