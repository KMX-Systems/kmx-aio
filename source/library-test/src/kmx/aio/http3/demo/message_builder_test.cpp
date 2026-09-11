/// @file src/kmx/aio/http3/demo/message_builder_test.cpp
/// @brief Unit tests for the HTTP/3 demo message builder: text payloads and HTTP/3 frames.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/http3/demo/message_builder.hpp>
#ifndef PCH
    #include <kmx/aio/http3/frame.hpp>
    #include <kmx/aio/http3/message.hpp>

    #include <catch2/catch_test_macros.hpp>
#endif

namespace kmx::aio::test::http3::demo::message_builder_test
{
    using namespace kmx::aio::http3;

    TEST_CASE("http3 demo request payload roundtrip", "[http3][codec][request]")
    {
        request_head request {
            .method = "POST",
            .scheme = "https",
            .authority = "example.test",
            .target = "/submit",
            .headers =
                {
                    {"Content-Type", "text/plain"},
                },
        };

        const auto payload = kmx::aio::http3::demo::message_builder::make_request_payload(request, "hello");
        const auto parsed = kmx::aio::http3::demo::message_builder::parse_request_payload(payload);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->head.method == "POST");
        REQUIRE(parsed->head.authority == "example.test");
        REQUIRE(parsed->head.target == "/submit");
        REQUIRE(parsed->body == "hello");
    }

    TEST_CASE("http3 demo request parser maps malformed message to message_error", "[http3][codec][request][errors]")
    {
        const auto parsed = kmx::aio::http3::demo::message_builder::parse_request_payload("GET / HTTP/0.9\r\nHost: example.test");
        REQUIRE_FALSE(parsed.has_value());
        REQUIRE(parsed.error() == make_error_code(error_code::message_error));
    }

    TEST_CASE("http3 demo response payload roundtrip", "[http3][codec][response]")
    {
        response_head response {
            .status = 404u,
            .headers =
                {
                    {"Content-Type", "text/plain"},
                },
        };

        const auto payload = kmx::aio::http3::demo::message_builder::make_response_payload(response, "missing");
        const auto parsed = kmx::aio::http3::demo::message_builder::parse_response_payload(payload);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->head.status == 404u);
        REQUIRE(parsed->body == "missing");
    }

    TEST_CASE("http3 demo request frames roundtrip", "[http3][codec][request][frame]")
    {
        request_head request {
            .method = "GET",
            .scheme = "https",
            .authority = "example.test",
            .target = "/index.html",
            .headers =
                {
                    {"accept", "text/html"},
                },
        };

        const auto encoded = kmx::aio::http3::demo::message_builder::make_request_frames(request, "body");
        const auto decoded = kmx::aio::http3::demo::message_builder::parse_request_frames(encoded);

        REQUIRE(decoded.has_value());
        REQUIRE(decoded->head.method == request.method);
        REQUIRE(decoded->head.authority == request.authority);
        REQUIRE(decoded->head.target == request.target);
        REQUIRE(decoded->body == "body");
    }

    TEST_CASE("http3 demo response frames roundtrip", "[http3][codec][response][frame]")
    {
        response_head response {
            .status = 200u,
            .headers =
                {
                    {"content-type", "text/plain"},
                },
        };

        const auto encoded = kmx::aio::http3::demo::message_builder::make_response_frames(response, "hello world");
        const auto decoded = kmx::aio::http3::demo::message_builder::parse_response_frames(encoded);

        REQUIRE(decoded.has_value());
        REQUIRE(decoded->head.status == response.status);
        REQUIRE(decoded->body == "hello world");
    }
}
