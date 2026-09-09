/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/cemi.hpp>
#include <kmx/aio/test/knx/telegram.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace kmx::aio::test::knx::cemi_test
{
    using namespace kmx::aio::knx;

    namespace detail
    {
        /// @brief The device the tests send from, 1.1.1.
        inline constexpr individual_address source {1u, 1u, 1u};
        /// @brief The group the tests send to, 1/2/3.
        inline constexpr group_address destination {0x0A03u};
    }

    TEST_CASE("knx cemi encodes a compact group write to the captured bytes", "[knx][cemi][unit]")
    {
        std::array<std::uint8_t, cemi::min_l_data_size> buffer {};
        const auto size = cemi::encode_group_value_write(buffer, detail::destination, apdu_payload::compact(1u), detail::source);

        REQUIRE(size.has_value());
        CHECK(*size == cemi::min_l_data_size);
        CHECK(buffer == sample_cemi);
    }

    TEST_CASE("knx cemi round-trips an extended payload", "[knx][cemi][unit]")
    {
        const std::array<std::uint8_t, 4u> value {0xDEu, 0xADu, 0xBEu, 0xEFu};
        const auto payload = apdu_payload::extended(value);
        REQUIRE(payload.has_value());
        CHECK(payload->data_length() == 5u);

        std::array<std::uint8_t, cemi::min_l_data_size + 4u> buffer {};
        const auto size = cemi::encode_group_value_write(buffer, detail::destination, *payload, detail::source);
        REQUIRE(size.has_value());
        CHECK(*size == buffer.size());

        const auto decoded = cemi::decode(buffer);
        REQUIRE(decoded.has_value());
        CHECK(!decoded->compact());
        CHECK(decoded->data_length == 5u);
        CHECK(decoded->payload_offset == cemi::min_l_data_size);
        CHECK(decoded->payload_size == value.size());
        CHECK(std::vector<std::uint8_t>(decoded->payload(buffer).begin(), decoded->payload(buffer).end()) ==
              std::vector<std::uint8_t>(value.begin(), value.end()));
    }

    TEST_CASE("knx cemi control fields carry the link layer flags", "[knx][cemi][unit]")
    {
        const l_data_options options {
            .telegram_priority = priority::urgent, .hop_count = 3u, .acknowledge_request = true, .repeat = false, .broadcast = false};
        std::array<std::uint8_t, cemi::min_l_data_size> buffer {};
        REQUIRE(cemi::encode_group_value_write(buffer, detail::destination, apdu_payload {}, detail::source, options).has_value());

        const auto decoded = cemi::decode(buffer);
        REQUIRE(decoded.has_value());
        CHECK(decoded->telegram_priority() == priority::urgent);
        CHECK(decoded->hop_count() == 3u);
        CHECK(decoded->acknowledge_requested());
        CHECK(!decoded->repeat_flag());
        CHECK(decoded->repetition());
        CHECK(!decoded->broadcast());
        CHECK(decoded->standard_frame());
    }

    TEST_CASE("knx cemi marks a long frame as extended", "[knx][cemi][unit]")
    {
        const std::vector<std::uint8_t> short_value(14u, 0xA5u);
        const std::vector<std::uint8_t> long_value(15u, 0xA5u);

        std::array<std::uint8_t, cemi::min_l_data_size + 15u> buffer {};
        const auto short_payload = apdu_payload::extended(short_value);
        REQUIRE(short_payload.has_value());
        REQUIRE(cemi::encode_group_value_write(buffer, detail::destination, *short_payload, detail::source).has_value());
        CHECK(cemi::decode(cspan_uint8_t {buffer.data(), cemi::min_l_data_size + 14u})->standard_frame());

        const auto long_payload = apdu_payload::extended(long_value);
        REQUIRE(long_payload.has_value());
        REQUIRE(cemi::encode_group_value_write(buffer, detail::destination, *long_payload, detail::source).has_value());

        const auto decoded = cemi::decode(buffer);
        REQUIRE(decoded.has_value());
        CHECK(!decoded->standard_frame());
        CHECK(decoded->data_length == 16u);
    }

    TEST_CASE("knx cemi addresses one device as well as a group", "[knx][cemi][unit]")
    {
        constexpr individual_address target {1u, 1u, 20u};
        std::array<std::uint8_t, cemi::min_l_data_size> buffer {};
        REQUIRE(cemi::encode(buffer, cemi_message_code::l_data_req, detail::source, target, apci::device_descriptor_read, apdu_payload {})
                    .has_value());

        const auto decoded = cemi::decode(buffer);
        REQUIRE(decoded.has_value());
        CHECK(!decoded->group_addressed());
        CHECK(decoded->address_type() == address_type::individual);
        CHECK(decoded->individual_destination() == target);
        CHECK(decoded->application_service == apci::device_descriptor_read);
    }

    TEST_CASE("knx cemi preserves an additional information block", "[knx][cemi][unit]")
    {
        // Two octets of additional information push every later field along, which is exactly what a
        // decoder that assumes a fixed offset gets wrong.
        std::array<std::uint8_t, cemi::min_l_data_size + 2u> buffer {};
        buffer[0u] = static_cast<std::uint8_t>(cemi_message_code::l_data_ind);
        buffer[1u] = 2u;
        buffer[2u] = 0x03u;
        buffer[3u] = 0x01u;
        buffer[4u] = 0xBCu;
        buffer[5u] = 0xE0u;
        buffer[6u] = 0x11u;
        buffer[7u] = 0x01u;
        buffer[8u] = 0x0Au;
        buffer[9u] = 0x03u;
        buffer[10u] = 0x01u;
        buffer[11u] = 0x00u;
        buffer[12u] = 0x81u;

        const auto decoded = cemi::decode(buffer);
        REQUIRE(decoded.has_value());
        CHECK(decoded->additional_info_length == 2u);
        CHECK(decoded->source == detail::source);
        CHECK(decoded->group_destination() == detail::destination);
        CHECK(decoded->application_service == apci::group_value_write);
        CHECK(decoded->compact_value == 1u);

        const auto info = cemi::additional_info(*decoded, buffer);
        REQUIRE(info.size() == 2u);
        CHECK(info[0u] == 0x03u);
        CHECK(info[1u] == 0x01u);
    }

    TEST_CASE("knx cemi identifies escaped application services", "[knx][cemi][unit]")
    {
        STATIC_CHECK(cemi::service_of(0x0081u) == apci::group_value_write);
        STATIC_CHECK(cemi::service_of(0x0000u) == apci::group_value_read);
        STATIC_CHECK(cemi::service_of(0x0043u) == apci::group_value_response);
        STATIC_CHECK(cemi::service_of(0x03D5u) == apci::property_value_read);
        STATIC_CHECK(cemi::service_of(0x02C2u) == apci::user_memory_write);
        STATIC_CHECK(cemi::service_of(0x0380u) == apci::restart);
    }

    TEST_CASE("knx cemi reports the transport control bits", "[knx][cemi][unit]")
    {
        auto buffer = sample_cemi;
        CHECK(cemi::decode(buffer)->unnumbered());

        buffer[9u] = 0x46u; // numbered data packet, sequence 1
        const auto decoded = cemi::decode(buffer);
        REQUIRE(decoded.has_value());
        CHECK(!decoded->unnumbered());
        CHECK(decoded->numbered());
        CHECK(decoded->sequence_number() == 1u);
    }

    TEST_CASE("knx cemi rejects what it cannot decode", "[knx][cemi][unit]")
    {
        CHECK(cemi::decode(cspan_uint8_t {}).error() == error::malformed_frame);
        CHECK(cemi::decode(cspan_uint8_t {sample_cemi.data(), 1u}).error() == error::malformed_frame);

        auto unsupported = sample_cemi;
        unsupported[0u] = static_cast<std::uint8_t>(cemi_message_code::l_busmon_ind);
        CHECK(cemi::decode(unsupported).error() == error::unsupported_message_code);

        auto control_apdu = sample_cemi;
        control_apdu[8u] = 0u; // a transport control APDU carries no application service
        CHECK(cemi::decode(control_apdu).error() == error::unsupported_service);

        auto wrong_length = sample_cemi;
        wrong_length[8u] = 4u;
        CHECK(cemi::decode(wrong_length).error() == error::invalid_length);

        auto overlong_info = sample_cemi;
        overlong_info[1u] = 200u;
        CHECK(cemi::decode(overlong_info).error() == error::malformed_frame);
    }

    TEST_CASE("knx cemi rejects an encode that does not fit", "[knx][cemi][unit]")
    {
        std::array<std::uint8_t, cemi::min_l_data_size - 1u> buffer {};
        CHECK(cemi::encode_group_value_write(buffer, detail::destination, apdu_payload {}, detail::source).error() ==
              error::invalid_length);

        std::array<std::uint8_t, cemi::min_l_data_size> ok {};
        CHECK(
            cemi::encode(ok, cemi_message_code::m_reset_req, detail::source, detail::destination, apci::group_value_write, apdu_payload {})
                .error() == error::unsupported_message_code);

        const l_data_options too_many_hops {.hop_count = 8u};
        CHECK(cemi::encode_group_value_write(ok, detail::destination, apdu_payload {}, detail::source, too_many_hops).error() ==
              error::invalid_configuration);
    }

    TEST_CASE("knx cemi payload view is empty for a foreign buffer", "[knx][cemi][unit]")
    {
        const auto decoded = cemi::decode(sample_cemi_temperature);
        REQUIRE(decoded.has_value());
        CHECK(decoded->payload(sample_cemi_temperature).size() == 2u);
        CHECK(decoded->payload(cspan_uint8_t {sample_cemi}).empty());
    }

    TEST_CASE("knx apdu payload refuses more octets than a frame can carry", "[knx][cemi][unit]")
    {
        const std::vector<std::uint8_t> value(apdu_payload::max_octets + 1u, 0u);
        CHECK(apdu_payload::extended(value).error() == error::payload_too_large);

        const std::vector<std::uint8_t> largest(apdu_payload::max_octets, 0u);
        const auto payload = apdu_payload::extended(largest);
        REQUIRE(payload.has_value());
        CHECK(payload->data_length() == 255u);
        CHECK(apdu_payload::extended(cspan_uint8_t {}).value().compacted());
    }

    TEST_CASE("knx cemi preserves compact and maximum extended data lengths", "[knx][cemi][unit]")
    {
        const auto compact = apdu_payload::compact(0x3Fu);
        CHECK(compact.compacted());
        CHECK(compact.data_length() == 1u);

        const std::vector<std::uint8_t> maximum(apdu_payload::max_octets, 0xA5u);
        const auto extended = apdu_payload::extended(maximum);
        REQUIRE(extended.has_value());
        CHECK(!extended->compacted());
        CHECK(extended->data_length() == 255u);

        std::array<std::uint8_t, cemi::max_l_data_size> buffer {};
        const auto encoded = cemi::encode_group_value_write(
            buffer, detail::destination, *extended, detail::source);
        REQUIRE(encoded.has_value());
        CHECK(*encoded == cemi::max_l_data_size);

        const auto decoded = cemi::decode(buffer);
        REQUIRE(decoded.has_value());
        CHECK(!decoded->standard_frame());
        CHECK(decoded->data_length == 255u);
        CHECK(decoded->payload_size == apdu_payload::max_octets);
    }

    // Device management is the other half of cEMI: the services that read and write a device's interface
    // object properties. They share nothing with L_Data - no addresses, no APCI - and were previously
    // rejected outright, which left the APCI names for property access with no transport to travel on.
    TEST_CASE("knx cemi encodes a property read request", "[knx][cemi][unit]")
    {
        // FC (M_PropRead.req) | 0000 (device object) | 01 (instance) | 33 (PID) | 1 element at index 1.
        const std::array<std::uint8_t, cemi::property_header_size> expected {
            0xFCu, 0x00u, 0x00u, 0x01u, 0x33u, 0x10u, 0x01u,
        };

        std::array<std::uint8_t, cemi::property_header_size> buffer {};
        const auto size = cemi::encode_property_read(buffer, 0u, 1u, 0x33u);
        REQUIRE(size.has_value());
        CHECK(*size == cemi::property_header_size);
        CHECK(buffer == expected);

        const auto decoded = cemi::decode_property(buffer);
        REQUIRE(decoded.has_value());
        CHECK(decoded->message_code == cemi_message_code::m_prop_read_req);
        CHECK(decoded->object_type == 0u);
        CHECK(decoded->object_instance == 1u);
        CHECK(decoded->property_id == 0x33u);
        CHECK(decoded->element_count == 1u);
        CHECK(decoded->start_index == 1u);
        CHECK(decoded->data(buffer).empty());
    }

    TEST_CASE("knx cemi round-trips a property write with data", "[knx][cemi][unit]")
    {
        const std::array<std::uint8_t, 2u> value {0x12u, 0x34u};
        std::array<std::uint8_t, cemi::property_header_size + value.size()> buffer {};
        const auto size = cemi::encode_property_write(buffer, 11u, 1u, 0x34u, value);
        REQUIRE(size.has_value());
        CHECK(*size == buffer.size());
        CHECK(buffer[0u] == static_cast<std::uint8_t>(cemi_message_code::m_prop_write_req));

        const auto decoded = cemi::decode_property(buffer);
        REQUIRE(decoded.has_value());
        CHECK(decoded->object_type == 11u);
        CHECK(decoded->property_id == 0x34u);
        const auto data = decoded->data(buffer);
        REQUIRE(data.size() == value.size());
        CHECK(std::equal(data.begin(), data.end(), value.begin()));
    }

    TEST_CASE("knx cemi reports a failed property confirmation", "[knx][cemi][unit]")
    {
        // A confirmation with no elements carries an error code where the data would be.
        const std::array<std::uint8_t, cemi::property_header_size + 1u> packet {
            0xFBu, 0x00u, 0x00u, 0x01u, 0x33u, 0x00u, 0x01u, 0x07u,
        };
        const auto decoded = cemi::decode_property(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->message_code == cemi_message_code::m_prop_read_con);
        CHECK(decoded->element_count == 0u);
        CHECK(decoded->failed());
        REQUIRE(decoded->data(packet).size() == 1u);
        CHECK(decoded->data(packet)[0u] == 0x07u);
    }

    TEST_CASE("knx cemi rejects a property service that is not one", "[knx][cemi][unit]")
    {
        std::array<std::uint8_t, cemi::property_header_size> buffer {};
        CHECK(cemi::encode_property(buffer, property_frame {cemi_message_code::l_data_req}).error() ==
              error::unsupported_message_code);
        CHECK(cemi::decode_property(sample_cemi).error() == error::unsupported_message_code);

        // The element count is four bits and the start index twelve; neither silently truncates.
        CHECK(cemi::encode_property(buffer, property_frame {cemi_message_code::m_prop_read_req, 0u, 1u, 0x33u, 0x10u, 1u}).error() ==
              error::invalid_configuration);
        CHECK(cemi::encode_property(buffer, property_frame {cemi_message_code::m_prop_read_req, 0u, 1u, 0x33u, 1u, 0x1000u}).error() ==
              error::invalid_configuration);
    }

    TEST_CASE("knx cemi encodes a reset request", "[knx][cemi][unit]")
    {
        std::array<std::uint8_t, 1u> buffer {};
        const auto size = cemi::encode_reset(buffer);
        REQUIRE(size.has_value());
        CHECK(*size == 1u);
        CHECK(buffer[0u] == 0xF1u);
        CHECK(cemi::encode_reset(buffer, cemi_message_code::l_data_req).error() == error::unsupported_message_code);
    }
}
