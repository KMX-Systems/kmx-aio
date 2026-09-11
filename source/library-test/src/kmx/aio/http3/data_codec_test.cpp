/// @file src/kmx/aio/http3/data_codec_test.cpp
/// @brief Unit tests for the HTTP/3 DATA frame codec.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/http3/data_codec.hpp>
#ifndef PCH
    #include <catch2/catch_test_macros.hpp>

    #include <cstdint>
    #include <vector>
#endif

namespace kmx::aio::test::http3::data_codec_test
{
    using namespace kmx::aio::http3;

    TEST_CASE("http3 data codec frame roundtrip", "[http3][codec][data][frame]")
    {
        const std::vector<std::uint8_t> body = {'o', 'k', '\n'};

        const auto encoded = data_codec::encode_frame(body);
        const auto decoded = data_codec::decode_frame(encoded);

        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == body);
    }
}
