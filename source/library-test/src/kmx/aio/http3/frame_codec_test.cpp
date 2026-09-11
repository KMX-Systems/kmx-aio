/// @file src/kmx/aio/http3/frame_codec_test.cpp
/// @brief Unit tests for the HTTP/3 frame envelope codec.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/http3/frame_codec.hpp>
#ifndef PCH
    #include <kmx/aio/http3/frame.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <cstdint>
    #include <vector>
#endif

namespace kmx::aio::test::http3::frame_codec_test
{
    using namespace kmx::aio::http3;

    TEST_CASE("http3 frame codec roundtrip", "[http3][codec][frame]")
    {
        const std::vector<std::uint8_t> payload = {0xDEu, 0xADu, 0xBEu, 0xEFu};
        const auto encoded = frame_codec::encode(frame_type::data, payload);
        const auto decoded = frame_codec::decode(encoded);

        REQUIRE(decoded.has_value());
        REQUIRE(decoded->type == frame_type::data);
        REQUIRE(decoded->payload == payload);
    }

    TEST_CASE("http3 frame codec maps malformed envelope to frame_error", "[http3][codec][frame][errors]")
    {
        // type=data (0x00), declared payload length=3, but only 1 byte present.
        const std::vector<std::uint8_t> malformed = {0x00u, 0x03u, 0xAAu};
        const auto decoded = frame_codec::decode(malformed);

        REQUIRE_FALSE(decoded.has_value());
        REQUIRE(decoded.error() == make_error_code(error_code::frame_error));
    }
}
