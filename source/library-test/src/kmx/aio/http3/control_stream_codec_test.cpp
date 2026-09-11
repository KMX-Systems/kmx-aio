/// @file src/kmx/aio/http3/control_stream_codec_test.cpp
/// @brief Unit tests for the HTTP/3 control stream codec: opening, GOAWAY and control stream rules.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/http3/control_stream_codec.hpp>
#ifndef PCH
    #include <kmx/aio/http3/control.hpp>
    #include <kmx/aio/http3/data_codec.hpp>
    #include <kmx/aio/http3/frame.hpp>
    #include <kmx/aio/http3/goaway_codec.hpp>
    #include <kmx/aio/http3/settings.hpp>
    #include <kmx/aio/http3/settings_codec.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <cstdint>
    #include <vector>
#endif

namespace kmx::aio::test::http3::control_stream_codec_test
{
    using namespace kmx::aio::http3;

    TEST_CASE("http3 control stream opening and goaway roundtrip", "[http3][control][goaway]")
    {
        settings initial {};
        initial.max_field_section_size = 8192u;
        initial.qpack_blocked_streams = 2u;
        const goaway_frame closing {.stream_id = 12u};

        const auto opening = control_stream_codec::encode_opening(initial);
        const auto full = control_stream_codec::append_goaway(opening, closing);
        const auto decoded = control_stream_codec::decode(full);

        REQUIRE(decoded.has_value());
        REQUIRE(decoded->saw_settings);
        REQUIRE(decoded->negotiated_settings.max_field_section_size == initial.max_field_section_size);
        REQUIRE(decoded->negotiated_settings.qpack_blocked_streams == initial.qpack_blocked_streams);
        REQUIRE(decoded->goaway.has_value());
        REQUIRE(decoded->goaway->stream_id == closing.stream_id);
    }

    TEST_CASE("http3 control stream requires SETTINGS as first frame", "[http3][control][protocol]")
    {
        std::vector<std::uint8_t> bytes {0x00u}; // control stream type
        const auto goaway = goaway_codec::encode_frame(goaway_frame {.stream_id = 5u});
        bytes.insert(bytes.end(), goaway.begin(), goaway.end());

        const auto decoded = control_stream_codec::decode(bytes);
        REQUIRE_FALSE(decoded.has_value());
        REQUIRE(decoded.error() == make_error_code(error_code::missing_settings));
    }

    TEST_CASE("http3 control stream rejects duplicate SETTINGS", "[http3][control][protocol]")
    {
        settings first {};
        first.max_field_section_size = 4096u;
        settings second {};
        second.max_field_section_size = 8192u;

        auto bytes = control_stream_codec::encode_opening(first);
        const auto second_settings = settings_codec::encode_frame(second);
        bytes.insert(bytes.end(), second_settings.begin(), second_settings.end());

        const auto decoded = control_stream_codec::decode(bytes);
        REQUIRE_FALSE(decoded.has_value());
        REQUIRE(decoded.error() == make_error_code(error_code::settings_error));
    }

    TEST_CASE("http3 control stream rejects request stream frames", "[http3][control][protocol]")
    {
        settings initial {};
        auto bytes = control_stream_codec::encode_opening(initial);
        const std::vector<std::uint8_t> body = {'b', 'a', 'd'};
        const auto data = data_codec::encode_frame(body);
        bytes.insert(bytes.end(), data.begin(), data.end());

        const auto decoded = control_stream_codec::decode(bytes);
        REQUIRE_FALSE(decoded.has_value());
        REQUIRE(decoded.error() == make_error_code(error_code::frame_unexpected));
    }
}
