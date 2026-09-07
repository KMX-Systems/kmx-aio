/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/error.hpp>
#include <kmx/aio/knx/frame.hpp>
#include <kmx/aio/test/knx/telegram.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace kmx::aio::test::knx::frame_test
{
    using namespace kmx::aio::knx;

    TEST_CASE("knx frame encodes and decodes a header", "[knx][frame][unit]")
    {
        std::array<std::uint8_t, 6u> encoded {};
        const auto result = frame::encode_communication_header(encoded, 0x0102u, 10u, 0x10u);
        REQUIRE(result.has_value());

        const auto decoded = frame::decode_communication_header(encoded);
        REQUIRE(decoded.has_value());
        CHECK(decoded->protocol_version == 0x10u);
        CHECK(decoded->service_type == 0x0102u);
        CHECK(decoded->total_length == 10u);
        CHECK(encoded[0] == 0x06u);
        CHECK(encoded[1] == 0x10u);
        CHECK(encoded[2] == 0x01u);
        CHECK(encoded[3] == 0x02u);
        CHECK(encoded[4] == 0x00u);
        CHECK(encoded[5] == 0x0Au);
    }

    TEST_CASE("knx frame rejects undersized headers", "[knx][frame][unit]")
    {
        std::array<std::uint8_t, 5u> bad {};
        const auto result = frame::decode_communication_header(bad);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx frame rejects a total length below the header size", "[knx][frame][unit]")
    {
        const std::array<std::uint8_t, 6u> bad { 0x06u, 0x10u, 0x01u, 0x02u, 0x00u, 0x05u };
        const auto result = frame::decode_communication_header(bad);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx frame rejects an unsupported protocol version", "[knx][frame][unit]")
    {
        const std::array<std::uint8_t, 6u> bad { 0x06u, 0x06u, 0x01u, 0x02u, 0x00u, 0x06u };
        const auto result = frame::decode_communication_header(bad);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::unsupported_service));
    }

    TEST_CASE("knx frame encoder rejects a total length below the header size", "[knx][frame][unit]")
    {
        std::array<std::uint8_t, 6u> destination {};
        const auto result = frame::encode_communication_header(destination, 0x0102u, 5u);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::invalid_length));
    }

    TEST_CASE("knx frame encoder rejects an unsupported protocol version", "[knx][frame][unit]")
    {
        std::array<std::uint8_t, 6u> destination {};
        const auto result = frame::encode_communication_header(destination, 0x0102u, 6u, 0x06u);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::unsupported_service));
    }

    TEST_CASE("knx frame decodes a valid cEMI frame", "[knx][frame][unit]")
    {
        const auto decoded = frame::decode_cemi(sample_cemi);
        REQUIRE(decoded.has_value());
        CHECK(decoded->message_code == cemi_message_code::l_data_req);
        CHECK(decoded->application_service == apci::group_value_write);
        CHECK(decoded->group_addressed());
        CHECK(decoded->compact_value == 1u);
    }

    TEST_CASE("knx frame rejects cEMI additional information beyond payload", "[knx][frame][unit]")
    {
        auto cemi = sample_cemi;
        cemi[1u] = 0x07u; // an additional information block the remaining octets cannot hold
        const auto result = frame::decode_cemi(cemi);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx frame rejects a cEMI frame cut short of its header", "[knx][frame][unit]")
    {
        const auto cemi = truncated_cemi();
        const auto result = frame::decode_cemi(cemi);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx frame rejects a cEMI length field that disagrees with the payload", "[knx][frame][unit]")
    {
        // The header survives the cut, so the frame is well formed up to the point where the declared
        // data length claims one octet more than the buffer holds.
        const cspan_uint8_t cemi {sample_cemi_temperature.data(), sample_cemi_temperature.size() - 1u};
        const auto result = frame::decode_cemi(cemi);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::invalid_length));
    }

    TEST_CASE("knx frame rejects an unsupported cEMI message code", "[knx][frame][unit]")
    {
        auto cemi = sample_cemi;
        cemi[0u] = static_cast<std::uint8_t>(cemi_message_code::m_prop_read_req);
        const auto result = frame::decode_cemi(cemi);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::unsupported_message_code));
    }

    TEST_CASE("knx tunnelling request round-trips channel metadata and cEMI", "[knx][frame][unit]")
    {
        std::array<std::uint8_t, frame::tunnelling_request_header_size + sample_cemi_size> packet {};

        const auto encoded = frame::encode_tunnelling_request(packet, 7u, 2u, sample_cemi);
        REQUIRE(encoded.has_value());

        const auto decoded = frame::decode_tunnelling_request(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->channel_id == 7u);
        CHECK(decoded->sequence_number == 2u);
        CHECK(decoded->cemi.message_code == cemi_message_code::l_data_req);
        CHECK(decoded->message_length == sample_cemi_size);
    }

    TEST_CASE("knx tunnelling ack decodes channel sequence and status", "[knx][frame][unit]")
    {
        const std::array<std::uint8_t, 4u> packet { 7u, 2u, 0u, 0u };
        const auto decoded = frame::decode_tunnelling_ack(packet);

        REQUIRE(decoded.has_value());
        CHECK(decoded->channel_id == 7u);
        CHECK(decoded->sequence_number == 2u);
        CHECK(decoded->status == 0u);
    }

    TEST_CASE("knx tunnelling ack packet round-trips", "[knx][frame][integration]")
    {
        std::array<std::uint8_t, 10u> packet {};
        REQUIRE(frame::encode_tunnelling_ack_packet(packet, 7u, 2u, 0x21u).has_value());

        const auto decoded = frame::decode_tunnelling_ack_packet(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->channel_id == 7u);
        CHECK(decoded->sequence_number == 2u);
        CHECK(decoded->status == 0x21u);
    }

    TEST_CASE("knx tunnelling request rejects non-zero reserved bytes", "[knx][frame][unit]")
    {
        std::array<std::uint8_t, frame::tunnelling_request_header_size + sample_cemi_size> request {};
        REQUIRE(frame::encode_tunnelling_request(request, 3u, 5u, sample_cemi).has_value());
        request[2u] = 0x01u; // the reserved octet of the connection header
        const auto result = frame::decode_tunnelling_request(request);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx tunnelling ack rejects non-zero reserved bytes", "[knx][frame][unit]")
    {
        const std::array<std::uint8_t, 4u> ack { 3u, 5u, 0u, 0x01u };
        const auto result = frame::decode_tunnelling_ack(ack);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx tunnelling encoders reject channel zero", "[knx][frame][unit]")
    {
        std::array<std::uint8_t, frame::tunnelling_request_header_size + sample_cemi_size> request {};
        const auto request_result = frame::encode_tunnelling_request(request, 0u, 1u, sample_cemi);
        REQUIRE(!request_result.has_value());
        CHECK(request_result.error() == make_error_code(error::invalid_configuration));

        std::array<std::uint8_t, 10u> ack {};
        const auto ack_result = frame::encode_tunnelling_ack_packet(ack, 0u, 1u);
        REQUIRE(!ack_result.has_value());
        CHECK(ack_result.error() == make_error_code(error::invalid_configuration));
    }

    TEST_CASE("knx tunnelling decoders reject channel zero", "[knx][frame][unit]")
    {
        std::array<std::uint8_t, frame::tunnelling_request_header_size + sample_cemi_size> request {};
        REQUIRE(frame::encode_tunnelling_request(request, 3u, 1u, sample_cemi).has_value());
        request[0u] = 0u; // channel zero is never a valid channel
        const auto request_result = frame::decode_tunnelling_request(request);
        REQUIRE(!request_result.has_value());
        CHECK(request_result.error() == make_error_code(error::malformed_frame));

        const std::array<std::uint8_t, 4u> ack { 0u, 1u, 0u, 0u };
        const auto ack_result = frame::decode_tunnelling_ack(ack);
        REQUIRE(!ack_result.has_value());
        CHECK(ack_result.error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx tunnelling encoder rejects an oversized frame", "[knx][frame][unit]")
    {
        const std::vector<std::uint8_t> cemi(frame::max_frame_size, 0u);
        std::vector<std::uint8_t> packet(frame::max_frame_size + frame::communication_header_size, 0u);
        const auto result = frame::encode_tunnelling_request_packet(packet, 1u, 1u, cemi);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::invalid_length));
    }
}
