/// @file src/kmx/aio/http3/headers_codec_test.cpp
/// @brief Unit tests for the HTTP/3 HEADERS frame codec.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/http3/headers_codec.hpp>
#ifndef PCH
    #include <kmx/aio/http3/message.hpp>

    #include <catch2/catch_test_macros.hpp>
#endif

namespace kmx::aio::test::http3::headers_codec_test
{
    using namespace kmx::aio::http3;

    TEST_CASE("http3 headers codec frame roundtrip", "[http3][codec][headers][frame]")
    {
        header_list headers {
            {":method", "GET"},
            {":path", "/health"},
            {"accept", "text/plain"},
        };

        const auto encoded = headers_codec::encode_frame(headers);
        const auto decoded = headers_codec::decode_frame(encoded);

        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == headers);
    }
}
