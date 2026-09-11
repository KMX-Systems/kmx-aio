/// @file src/kmx/aio/knx/frame_test.cpp
/// @brief Unit tests for KNXnet/IP headers, tunnelling and device configuration frames, and tunnelling feature services.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/frame.hpp>
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/cemi.hpp>
    #include <kmx/aio/knx/cemi_bytes_storage.hpp>
    #include <kmx/aio/knx/cemi_frame.hpp>
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/tunnelling_feature_value.hpp>
    #include <kmx/aio/test/knx/telegram.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <algorithm>
    #include <array>
    #include <cstdint>
    #include <vector>
#endif

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
        const std::array<std::uint8_t, 6u> bad {0x06u, 0x10u, 0x01u, 0x02u, 0x00u, 0x05u};
        const auto result = frame::decode_communication_header(bad);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx frame rejects an unsupported protocol version", "[knx][frame][unit]")
    {
        const std::array<std::uint8_t, 6u> bad {0x06u, 0x06u, 0x01u, 0x02u, 0x00u, 0x06u};
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

    // Golden wire vectors for the connection header, the counterpart of the cEMI and routing ones. Encoder
    // and decoder agreed with each other for the whole life of this module while both omitted the structure
    // length octet, which shifted the channel id and sequence counter one octet early. Only captured bytes
    // catch that, because a round-trip test cannot.
    //
    // KNX System Specifications, 03/08/02 "Core", Connection Header.
    TEST_CASE("knx tunnelling request begins with a four-octet connection header", "[knx][frame][unit]")
    {
        // 06 10 | 04 20 | 00 15, then 04 (structure length) 07 (channel) 02 (sequence) 00 (reserved).
        const std::array<std::uint8_t, frame::communication_header_size + frame::tunnelling_request_header_size + sample_cemi_size>
            expected {
                0x06u, 0x10u, 0x04u, 0x20u, 0x00u, 0x15u, 0x04u, 0x07u, 0x02u, 0x00u, 0x11u,
                0x00u, 0xBCu, 0xE0u, 0x11u, 0x01u, 0x0Au, 0x03u, 0x01u, 0x00u, 0x81u,
        };

        std::array<std::uint8_t, expected.size()> encoded {};
        REQUIRE(frame::encode_tunnelling_request_packet(encoded, 7u, 2u, sample_cemi).has_value());
        CHECK(encoded == expected);

        const auto decoded = frame::decode_tunnelling_request_packet(expected);
        REQUIRE(decoded.has_value());
        CHECK(decoded->channel_id == 7u);
        CHECK(decoded->sequence_number == 2u);
    }

    TEST_CASE("knx tunnelling ack begins with a four-octet connection header", "[knx][frame][unit]")
    {
        // 06 10 | 04 21 | 00 0A, then 04 (structure length) 07 (channel) 02 (sequence) 21 (status).
        const std::array<std::uint8_t, frame::communication_header_size + frame::tunnelling_ack_size> expected {
            0x06u, 0x10u, 0x04u, 0x21u, 0x00u, 0x0Au, 0x04u, 0x07u, 0x02u, 0x21u,
        };

        std::array<std::uint8_t, expected.size()> encoded {};
        REQUIRE(frame::encode_tunnelling_ack_packet(encoded, 7u, 2u, 0x21u).has_value());
        CHECK(encoded == expected);

        const auto decoded = frame::decode_tunnelling_ack_packet(expected);
        REQUIRE(decoded.has_value());
        CHECK(decoded->channel_id == 7u);
        CHECK(decoded->sequence_number == 2u);
        CHECK(decoded->status == 0x21u);
    }

    TEST_CASE("knx tunnelling ack decodes channel sequence and status", "[knx][frame][unit]")
    {
        // [structure length, channel id, sequence counter, status]
        const std::array<std::uint8_t, 4u> packet {0x04u, 7u, 2u, 0u};
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
        request[3u] = 0x01u; // the reserved octet of the connection header, after channel and sequence
        const auto result = frame::decode_tunnelling_request(request);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::malformed_frame));
    }

    // An acknowledgement has no reserved octet: its fourth field is the status. What must be rejected is a
    // connection header whose structure length is not four, which is what an implementation that omits the
    // length octet emits - the channel id lands where the length belongs.
    TEST_CASE("knx tunnelling ack rejects a bad structure length", "[knx][frame][unit]")
    {
        const std::array<std::uint8_t, 4u> ack {3u, 5u, 0u, 0x00u};
        const auto result = frame::decode_tunnelling_ack(ack);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx tunnelling ack carries a non-zero status", "[knx][frame][unit]")
    {
        const std::array<std::uint8_t, 4u> ack {0x04u, 3u, 5u, 0x21u};
        const auto decoded = frame::decode_tunnelling_ack(ack);
        REQUIRE(decoded.has_value());
        CHECK(decoded->channel_id == 3u);
        CHECK(decoded->sequence_number == 5u);
        CHECK(decoded->status == 0x21u);
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
        request[1u] = 0u; // channel zero is never a valid channel
        const auto request_result = frame::decode_tunnelling_request(request);
        REQUIRE(!request_result.has_value());
        CHECK(request_result.error() == make_error_code(error::malformed_frame));

        const std::array<std::uint8_t, 4u> ack {0x04u, 0u, 1u, 0u};
        const auto ack_result = frame::decode_tunnelling_ack(ack);
        REQUIRE(!ack_result.has_value());
        CHECK(ack_result.error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx tunnelling encoder rejects an oversized frame", "[knx][frame][unit]")
    {
        const std::vector<std::uint8_t> cemi(frame::max_total_length, 0u);
        std::vector<std::uint8_t> packet(frame::max_total_length + frame::communication_header_size, 0u);
        const auto result = frame::encode_tunnelling_request_packet(packet, 1u, 1u, cemi);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::invalid_length));
    }

    /// @brief Builds a tunnelling request whose cEMI carries both variable fields at a chosen size.
    /// @param additional_info_length The additional information block length octet.
    /// @param data_length The cEMI data length octet.
    /// @return The complete KNXnet/IP packet.
    [[nodiscard]] static std::vector<std::uint8_t> tunnelling_packet_with(const std::uint8_t additional_info_length,
                                                                          const std::uint8_t data_length)
    {
        const std::size_t cemi_size = cemi::encoded_size(additional_info_length, data_length);
        const std::size_t total = frame::communication_header_size + frame::tunnelling_request_header_size + cemi_size;

        std::vector<std::uint8_t> packet(total, 0x5Au);
        REQUIRE(
            frame::encode_communication_header(packet, frame::tunnelling_request_service, static_cast<std::uint16_t>(total)).has_value());
        packet[6u] = 0x04u; // connection header structure length
        packet[7u] = 0x01u; // channel id
        packet[8u] = 0x00u; // sequence counter
        packet[9u] = 0x00u; // reserved
        auto* const cemi_bytes = packet.data() + frame::communication_header_size + frame::tunnelling_request_header_size;
        cemi_bytes[0u] = static_cast<std::uint8_t>(cemi_message_code::l_data_ind);
        cemi_bytes[1u] = additional_info_length;
        cemi_bytes[cemi::prologue_size + additional_info_length + 6u] = data_length;
        return packet;
    }

    // A peer chooses both the additional information length and the data length, and both are one octet
    // wide, so the largest cEMI message that reaches the decoder is 255 octets longer than the largest one
    // this build's encoder produces. Decoding used to copy it into storage sized for the encoder's maximum.
    TEST_CASE("knx tunnelling decoder accepts a maximal cEMI message", "[knx][frame][unit]")
    {
        const auto packet = tunnelling_packet_with(static_cast<std::uint8_t>(cemi::max_additional_info_size), 0xFFu);
        REQUIRE(packet.size() == frame::communication_header_size + frame::tunnelling_request_header_size + cemi::max_message_size);

        const auto decoded = frame::decode_tunnelling_request_packet(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->message_length == cemi::max_message_size);
        CHECK(decoded->cemi_bytes.length() == cemi::max_message_size);
        CHECK(decoded->cemi_bytes.length() <= decoded->cemi_bytes.bytes.size());
        CHECK(decoded->cemi.additional_info_length == cemi::max_additional_info_size);
    }

    // Encoder and decoder have to agree on the maximum, or the library builds frames its own decoder - and
    // every peer's - must reject.
    TEST_CASE("knx tunnelling encoder and decoder share one cEMI maximum", "[knx][frame][unit]")
    {
        static_assert(frame::cemi_max_size == cemi::max_message_size,
                      "the framing layer must bound a cEMI message where the cEMI layer does");

        const auto packet = tunnelling_packet_with(static_cast<std::uint8_t>(cemi::max_additional_info_size), 0xFFu);
        const cspan_uint8_t cemi_bytes {packet.data() + frame::communication_header_size + frame::tunnelling_request_header_size,
                                        cemi::max_message_size};

        std::vector<std::uint8_t> encoded(packet.size(), 0u);
        REQUIRE(frame::encode_tunnelling_request_packet(encoded, 1u, 0u, cemi_bytes).has_value());

        CHECK(encoded == packet);

        const auto decoded = frame::decode_tunnelling_request_packet(encoded);
        REQUIRE(decoded.has_value());
        CHECK(decoded->cemi_bytes.length() == cemi::max_message_size);
        CHECK(std::equal(cemi_bytes.begin(), cemi_bytes.end(), decoded->cemi_bytes.begin()));
    }

    TEST_CASE("knx tunnelling decoder rejects a cEMI message beyond its storage", "[knx][frame][unit]")
    {
        auto packet = tunnelling_packet_with(static_cast<std::uint8_t>(cemi::max_additional_info_size), 0xFFu);
        packet.push_back(0u); // one octet past what the length fields can describe
        const auto total = static_cast<std::uint16_t>(packet.size());
        packet[4u] = static_cast<std::uint8_t>(total >> 8u);
        packet[5u] = static_cast<std::uint8_t>(total & 0xFFu);

        const auto decoded = frame::decode_tunnelling_request_packet(packet);
        REQUIRE(!decoded.has_value());
        CHECK(decoded.error() == make_error_code(error::invalid_length));
    }

    // DEVICE_CONFIGURATION_REQUEST is the tunnelling connection machinery over a management channel: the
    // same connection header, a cEMI device management payload rather than L_Data. Its minimum body is one
    // octet, because M_Reset.req is nothing but a message code.
    TEST_CASE("knx device configuration request round-trips a property read", "[knx][frame][unit]")
    {
        std::array<std::uint8_t, cemi::property_header_size> message {};
        REQUIRE(cemi::encode_property_read(message, {.object_type = 0u, .object_instance = 1u, .property_id = 0x33u}).has_value());

        const std::array<std::uint8_t, frame::communication_header_size + frame::tunnelling_request_header_size + message.size()> expected {
            0x06u, 0x10u, 0x03u, 0x10u, 0x00u, 0x11u, 0x04u, 0x07u, 0x02u, 0x00u, 0xFCu, 0x00u, 0x00u, 0x01u, 0x33u, 0x10u, 0x01u,
        };

        std::array<std::uint8_t, expected.size()> encoded {};
        REQUIRE(frame::encode_device_configuration_request_packet(encoded, 7u, 2u, message).has_value());
        CHECK(encoded == expected);

        const auto decoded = frame::decode_device_configuration_request_packet(encoded);
        REQUIRE(decoded.has_value());
        CHECK(decoded->channel_id == 7u);
        CHECK(decoded->sequence_number == 2u);
        CHECK(decoded->cemi_bytes.length() == message.size());

        const auto property = cemi::decode_property(decoded->cemi_bytes.span());
        REQUIRE(property.has_value());
        CHECK(property->property_id == 0x33u);
    }

    TEST_CASE("knx device configuration carries a bare reset", "[knx][frame][unit]")
    {
        std::array<std::uint8_t, 1u> message {};
        REQUIRE(cemi::encode_reset(message).has_value());

        std::array<std::uint8_t, frame::communication_header_size + frame::tunnelling_request_header_size + 1u> encoded {};
        REQUIRE(frame::encode_device_configuration_request_packet(encoded, 3u, 0u, message).has_value());

        const auto decoded = frame::decode_device_configuration_request_packet(encoded);
        REQUIRE(decoded.has_value());
        CHECK(decoded->cemi_bytes.length() == 1u);
        CHECK(decoded->cemi_bytes.span()[0u] == 0xF1u);
    }

    TEST_CASE("knx device configuration ack round-trips", "[knx][frame][unit]")
    {
        const std::array<std::uint8_t, frame::communication_header_size + frame::tunnelling_ack_size> expected {
            0x06u, 0x10u, 0x03u, 0x11u, 0x00u, 0x0Au, 0x04u, 0x07u, 0x02u, 0x00u,
        };

        std::array<std::uint8_t, expected.size()> encoded {};
        REQUIRE(frame::encode_device_configuration_ack_packet(encoded, 7u, 2u).has_value());
        CHECK(encoded == expected);

        const auto decoded = frame::decode_device_configuration_ack_packet(encoded);
        REQUIRE(decoded.has_value());
        CHECK(decoded->channel_id == 7u);
        CHECK(decoded->sequence_number == 2u);
        CHECK(decoded->status == 0u);
    }

    TEST_CASE("knx device configuration rejects an empty payload", "[knx][frame][unit]")
    {
        std::array<std::uint8_t, 16u> encoded {};
        const auto result = frame::encode_device_configuration_request_packet(encoded, 1u, 0u, {});
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::invalid_length));
    }

    // The four tunnelling feature services share one shape: connection header, feature identifier, a return
    // code that is reserved in all but the response, then a value that only some of them carry.
    TEST_CASE("knx tunnelling feature get carries no value", "[knx][frame][unit]")
    {
        // 06 10 | 04 22 | 00 0C, then 04 07 02 00 (connection header), 03 (bus status), 00 (reserved).
        const std::array<std::uint8_t, 12u> expected {
            0x06u, 0x10u, 0x04u, 0x22u, 0x00u, 0x0Cu, 0x04u, 0x07u, 0x02u, 0x00u, 0x03u, 0x00u,
        };

        const tunnelling_feature_frame request {
            frame::tunnelling_feature_get_service,
            7u,
            2u,
            tunnelling_feature::bus_connection_status,
        };
        std::array<std::uint8_t, expected.size()> encoded {};
        REQUIRE(frame::encode_tunnelling_feature_packet(encoded, request).has_value());
        CHECK(encoded == expected);

        const auto decoded = frame::decode_tunnelling_feature_packet(expected);
        REQUIRE(decoded.has_value());
        CHECK(decoded->service_type == frame::tunnelling_feature_get_service);
        CHECK(decoded->channel_id == 7u);
        CHECK(decoded->sequence_number == 2u);
        CHECK(decoded->feature == tunnelling_feature::bus_connection_status);
        CHECK(decoded->value.empty());
    }

    TEST_CASE("knx tunnelling feature response carries a value", "[knx][frame][unit]")
    {
        const std::array<std::uint8_t, 2u> value {0x11u, 0x01u};
        const tunnelling_feature_frame response {
            frame::tunnelling_feature_response_service,
            7u,
            3u,
            tunnelling_feature::individual_address,
        };

        std::array<std::uint8_t, 14u> encoded {};
        REQUIRE(frame::encode_tunnelling_feature_packet(encoded, response, value).has_value());
        CHECK(encoded[3u] == 0x23u);
        CHECK(encoded[10u] == 0x06u); // feature identifier
        CHECK(encoded[11u] == 0x00u); // return code, success

        const auto decoded = frame::decode_tunnelling_feature_packet(encoded);
        REQUIRE(decoded.has_value());
        CHECK(decoded->return_code == 0u);
        REQUIRE(decoded->value.length() == 2u);
        CHECK(decoded->value.span()[0u] == 0x11u);
    }

    TEST_CASE("knx tunnelling feature response reports a failure without a value", "[knx][frame][unit]")
    {
        // A failing response carries the return code instead of the value it could not produce.
        tunnelling_feature_frame response {
            frame::tunnelling_feature_response_service,
            7u,
            3u,
            tunnelling_feature::max_apdu_length,
        };
        response.return_code = 0x21u;

        std::array<std::uint8_t, 12u> encoded {};
        REQUIRE(frame::encode_tunnelling_feature_packet(encoded, response).has_value());
        CHECK(encoded[11u] == 0x21u);

        const auto decoded = frame::decode_tunnelling_feature_packet(encoded);
        REQUIRE(decoded.has_value());
        CHECK(decoded->return_code == 0x21u);
        CHECK(decoded->value.empty());
    }

    TEST_CASE("knx tunnelling feature enforces which services carry a value", "[knx][frame][unit]")
    {
        std::array<std::uint8_t, 16u> encoded {};
        const std::array<std::uint8_t, 1u> value {0x01u};

        // A get asks a question; a value in it is not a get.
        CHECK(frame::encode_tunnelling_feature_packet(
                  encoded,
                  tunnelling_feature_frame {frame::tunnelling_feature_get_service, 7u, 0u, tunnelling_feature::bus_connection_status},
                  value)
                  .error() == make_error_code(error::malformed_frame));

        // A set without a value names nothing to write.
        CHECK(frame::encode_tunnelling_feature_packet(encoded, tunnelling_feature_frame {frame::tunnelling_feature_set_service, 7u, 0u,
                                                                                         tunnelling_feature::info_service_enable})
                  .error() == make_error_code(error::malformed_frame));

        // And the same rules on the way in.
        const std::array<std::uint8_t, 13u> get_with_value {
            0x06u, 0x10u, 0x04u, 0x22u, 0x00u, 0x0Du, 0x04u, 0x07u, 0x02u, 0x00u, 0x03u, 0x00u, 0x01u,
        };
        CHECK(frame::decode_tunnelling_feature_packet(get_with_value).error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx tunnelling feature rejects an unknown identifier", "[knx][frame][unit]")
    {
        const std::array<std::uint8_t, 12u> packet {
            0x06u, 0x10u, 0x04u, 0x22u, 0x00u, 0x0Cu, 0x04u, 0x07u, 0x02u, 0x00u, 0x7Fu, 0x00u,
        };
        CHECK(frame::decode_tunnelling_feature_packet(packet).error() == make_error_code(error::unsupported_service));
    }

    TEST_CASE("knx tunnelling feature bounds an oversized value", "[knx][frame][unit]")
    {
        // The value storage is inline, so its capacity is a hard bound and not an assumption about peers.
        std::vector<std::uint8_t> packet(6u + 4u + 2u + tunnelling_feature_value::capacity + 1u, 0u);
        packet[0u] = 0x06u;
        packet[1u] = 0x10u;
        packet[2u] = 0x04u;
        packet[3u] = 0x25u; // INFO
        packet[4u] = static_cast<std::uint8_t>(packet.size() >> 8u);
        packet[5u] = static_cast<std::uint8_t>(packet.size() & 0xFFu);
        packet[6u] = 0x04u;
        packet[7u] = 0x07u;
        packet[8u] = 0x02u;
        packet[9u] = 0x00u;
        packet[10u] = 0x01u;

        CHECK(frame::decode_tunnelling_feature_packet(packet).error() == make_error_code(error::invalid_length));
    }
}
