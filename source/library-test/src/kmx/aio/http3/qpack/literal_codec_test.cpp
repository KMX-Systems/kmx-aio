/// @file src/kmx/aio/http3/qpack/literal_codec_test.cpp
/// @brief Unit tests for the HTTP/3 QPACK literal header codec and its static table lookups.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/http3/qpack/literal_codec.hpp>
#ifndef PCH
    #include <kmx/aio/http3/frame.hpp>
    #include <kmx/aio/http3/message.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <cstdint>
    #include <vector>
#endif

namespace kmx::aio::test::http3::qpack::literal_codec_test
{
    using namespace kmx::aio::http3;

    TEST_CASE("http3 qpack literal codec roundtrip", "[http3][qpack][literal]")
    {
        header_list headers {
            {":method", "GET"},
            {":authority", "example.test"},
            {"accept", "text/html"},
        };

        const auto encoded = kmx::aio::http3::qpack::literal_codec::encode(headers);
        const auto decoded = kmx::aio::http3::qpack::literal_codec::decode(encoded);

        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == headers);
    }

    TEST_CASE("http3 qpack literal decoder maps malformed payload to message_error", "[http3][qpack][literal][errors]")
    {
        const std::vector<std::uint8_t> malformed = {0x00u};
        const auto decoded = kmx::aio::http3::qpack::literal_codec::decode(malformed);

        REQUIRE_FALSE(decoded.has_value());
        REQUIRE(decoded.error() == make_error_code(error_code::message_error));
    }

    TEST_CASE("http3 qpack static table lookup finds common pseudo headers", "[http3][qpack][static-table]")
    {
        REQUIRE(kmx::aio::http3::qpack::literal_codec::static_field_index(":method", "GET").has_value());
        REQUIRE(kmx::aio::http3::qpack::literal_codec::static_field_index(":scheme", "https").has_value());
        REQUIRE(kmx::aio::http3::qpack::literal_codec::static_name_index(":authority").has_value());
    }
}
