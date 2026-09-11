/// @file src/kmx/aio/http3/settings_codec_test.cpp
/// @brief Unit tests for the HTTP/3 SETTINGS payload and frame codec.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/http3/settings_codec.hpp>
#ifndef PCH
    #include <kmx/aio/http3/data_codec.hpp>
    #include <kmx/aio/http3/frame.hpp>
    #include <kmx/aio/http3/settings.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <cstdint>
    #include <vector>
#endif

namespace kmx::aio::test::http3::settings_codec_test
{
    using namespace kmx::aio::http3;

    TEST_CASE("http3 settings codec roundtrip", "[http3][codec][settings]")
    {
        settings original {};
        original.qpack_max_table_capacity = 1024u;
        original.max_field_section_size = 32768u;
        original.qpack_blocked_streams = 8u;
        original.enable_connect_protocol = true;
        original.h3_datagram = true;

        const auto encoded = settings_codec::encode(original);
        const auto decoded = settings_codec::decode(encoded);

        REQUIRE(decoded.has_value());
        REQUIRE(decoded->qpack_max_table_capacity == original.qpack_max_table_capacity);
        REQUIRE(decoded->max_field_section_size == original.max_field_section_size);
        REQUIRE(decoded->qpack_blocked_streams == original.qpack_blocked_streams);
        REQUIRE(decoded->enable_connect_protocol == original.enable_connect_protocol);
        REQUIRE(decoded->h3_datagram == original.h3_datagram);
    }

    TEST_CASE("http3 settings decoder maps malformed payload to settings_error", "[http3][codec][settings][errors]")
    {
        const std::vector<std::uint8_t> malformed = {0x01u};
        const auto decoded = settings_codec::decode(malformed);

        REQUIRE_FALSE(decoded.has_value());
        REQUIRE(decoded.error() == make_error_code(error_code::settings_error));
    }

    TEST_CASE("http3 settings frame roundtrip", "[http3][codec][settings][frame]")
    {
        settings original {};
        original.qpack_max_table_capacity = 2048u;
        original.max_field_section_size = 65535u;
        original.qpack_blocked_streams = 4u;

        const auto encoded = settings_codec::encode_frame(original);
        const auto decoded = settings_codec::decode_frame(encoded);

        REQUIRE(decoded.has_value());
        REQUIRE(decoded->qpack_max_table_capacity == original.qpack_max_table_capacity);
        REQUIRE(decoded->max_field_section_size == original.max_field_section_size);
        REQUIRE(decoded->qpack_blocked_streams == original.qpack_blocked_streams);
    }

    TEST_CASE("http3 settings frame decoder rejects wrong frame type", "[http3][codec][settings][frame][errors]")
    {
        const std::vector<std::uint8_t> body = {'x'};
        const auto frame = data_codec::encode_frame(body);
        const auto decoded = settings_codec::decode_frame(frame);

        REQUIRE_FALSE(decoded.has_value());
        REQUIRE(decoded.error() == make_error_code(error_code::frame_unexpected));
    }
}
