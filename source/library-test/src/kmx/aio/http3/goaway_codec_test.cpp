/// @file src/kmx/aio/http3/goaway_codec_test.cpp
/// @brief Unit tests for the HTTP/3 GOAWAY payload and frame codec.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/http3/goaway_codec.hpp>
#ifndef PCH
    #include <kmx/aio/http3/data_codec.hpp>
    #include <kmx/aio/http3/frame.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <cstdint>
    #include <vector>
#endif

namespace kmx::aio::test::http3::goaway_codec_test
{
    using namespace kmx::aio::http3;

    TEST_CASE("http3 goaway decoder maps malformed payload to id_error", "[http3][codec][goaway][errors]")
    {
        const std::vector<std::uint8_t> malformed = {0x40u};
        const auto decoded = goaway_codec::decode(malformed);

        REQUIRE_FALSE(decoded.has_value());
        REQUIRE(decoded.error() == make_error_code(error_code::id_error));
    }

    TEST_CASE("http3 goaway decoder rejects trailing bytes", "[http3][codec][goaway][errors]")
    {
        auto payload = goaway_codec::encode(goaway_frame {.stream_id = 7u});
        payload.push_back(0x00u);
        const auto decoded = goaway_codec::decode(payload);

        REQUIRE_FALSE(decoded.has_value());
        REQUIRE(decoded.error() == make_error_code(error_code::id_error));
    }

    TEST_CASE("http3 goaway frame decoder rejects wrong frame type", "[http3][codec][goaway][frame][errors]")
    {
        const std::vector<std::uint8_t> body = {'x'};
        const auto frame = data_codec::encode_frame(body);
        const auto decoded = goaway_codec::decode_frame(frame);

        REQUIRE_FALSE(decoded.has_value());
        REQUIRE(decoded.error() == make_error_code(error_code::frame_unexpected));
    }
}
