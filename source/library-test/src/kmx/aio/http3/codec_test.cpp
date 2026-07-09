#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/http3/codec.hpp>
#include <kmx/aio/http3/stream.hpp>

namespace kmx::aio::http3::test
{
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

        const auto payload = demo::message_builder::make_request_payload(request, "hello");
        const auto parsed = demo::message_builder::parse_request_payload(payload);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->head.method == "POST");
        REQUIRE(parsed->head.authority == "example.test");
        REQUIRE(parsed->head.target == "/submit");
        REQUIRE(parsed->body == "hello");
    }

    TEST_CASE("http3 demo request parser maps malformed message to message_error", "[http3][codec][request][errors]")
    {
        const auto parsed = demo::message_builder::parse_request_payload("GET / HTTP/0.9\r\nHost: example.test");
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

        const auto payload = demo::message_builder::make_response_payload(response, "missing");
        const auto parsed = demo::message_builder::parse_response_payload(payload);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->head.status == 404u);
        REQUIRE(parsed->body == "missing");
    }

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

    TEST_CASE("http3 data codec frame roundtrip", "[http3][codec][data][frame]")
    {
        const std::vector<std::uint8_t> body = {'o', 'k', '\n'};

        const auto encoded = data_codec::encode_frame(body);
        const auto decoded = data_codec::decode_frame(encoded);

        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == body);
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

    TEST_CASE("http3 qpack literal codec roundtrip", "[http3][qpack][literal]")
    {
        header_list headers {
            {":method", "GET"},
            {":authority", "example.test"},
            {"accept", "text/html"},
        };

        const auto encoded = qpack::literal_codec::encode(headers);
        const auto decoded = qpack::literal_codec::decode(encoded);

        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == headers);
    }

    TEST_CASE("http3 qpack literal decoder maps malformed payload to message_error", "[http3][qpack][literal][errors]")
    {
        const std::vector<std::uint8_t> malformed = {0x00u};
        const auto decoded = qpack::literal_codec::decode(malformed);

        REQUIRE_FALSE(decoded.has_value());
        REQUIRE(decoded.error() == make_error_code(error_code::message_error));
    }

    TEST_CASE("http3 qpack static table lookup finds common pseudo headers", "[http3][qpack][static-table]")
    {
        REQUIRE(qpack::literal_codec::static_field_index(":method", "GET").has_value());
        REQUIRE(qpack::literal_codec::static_field_index(":scheme", "https").has_value());
        REQUIRE(qpack::literal_codec::static_name_index(":authority").has_value());
    }

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

    TEST_CASE("http3 stream state machine tracks half-close transitions", "[http3][stream][state]")
    {
        stream s {0u};
        REQUIRE(s.state() == stream_state::idle);

        s.on_frame_sent(frame_type::headers);
        REQUIRE(s.state() == stream_state::open);

        s.on_send_fin();
        REQUIRE(s.state() == stream_state::half_closed_local);

        s.on_recv_fin();
        REQUIRE(s.state() == stream_state::closed);
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

        const auto encoded = demo::message_builder::make_request_frames(request, "body");
        const auto decoded = demo::message_builder::parse_request_frames(encoded);

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

        const auto encoded = demo::message_builder::make_response_frames(response, "hello world");
        const auto decoded = demo::message_builder::parse_response_frames(encoded);

        REQUIRE(decoded.has_value());
        REQUIRE(decoded->head.status == response.status);
        REQUIRE(decoded->body == "hello world");
    }
}