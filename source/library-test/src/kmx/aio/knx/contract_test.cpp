/// @file src/kmx/aio/knx/contract_test.cpp
/// @brief Unit tests for the KNX contract macros and require(), plus routing configuration and control frames.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#define KMX_AIO_EXPECTS(condition) static_cast<void>(condition)
#define KMX_AIO_ENSURES(condition) static_cast<void>(condition)
#include <kmx/aio/knx/contract.hpp>
#ifndef PCH
    #include <kmx/aio/knx/routing.hpp>
    #include <kmx/aio/test/knx/telegram.hpp>

    #include <catch2/catch_test_macros.hpp>
#endif

namespace kmx::aio::test::knx::contract_test
{
    TEST_CASE("knx contract macros preserve caller overrides", "[knx][contract][unit]")
    {
        bool checked {};
        KMX_AIO_EXPECTS((checked = true));
        KMX_AIO_ENSURES(checked);
        CHECK(checked);
    }

    TEST_CASE("knx contract require preserves a valid value", "[knx][contract][unit]")
    {
        constexpr auto value = kmx::aio::knx::require(true, 42, "value must be present");
        static_assert(value == 42);
        CHECK(value == 42);
    }

    TEST_CASE("knx routing configuration validates multicast policy", "[knx][routing][unit]")
    {
        CHECK(kmx::aio::knx::routing::validate({}).has_value());
        CHECK(kmx::aio::knx::routing::validate(kmx::aio::knx::routing::multicast_configuration {.group = {192u, 0u, 2u, 1u}}).error() ==
              kmx::aio::knx::error::invalid_configuration);
        CHECK(kmx::aio::knx::routing::validate(kmx::aio::knx::routing::multicast_configuration {.port = 0u}).error() ==
              kmx::aio::knx::error::invalid_configuration);
    }

    TEST_CASE("knx routing indication round-trips cEMI", "[knx][routing][integration]")
    {
        std::array<std::uint8_t, 512u> packet {};
        const kmx::aio::knx::routing::indication value {sample_cemi};
        REQUIRE(kmx::aio::knx::routing::encode_indication_packet(packet, value).has_value());
        const auto decoded = kmx::aio::knx::routing::decode_indication_packet(
            {packet.data(), kmx::aio::knx::frame::communication_header_size + sample_cemi.size()});
        REQUIRE(decoded.has_value());
        CHECK(decoded->cemi_bytes.size() == sample_cemi.size());
    }

    TEST_CASE("knx routing busy and lost-message controls round-trip", "[knx][routing][unit]")
    {
        std::array<std::uint8_t, kmx::aio::knx::frame::communication_header_size + kmx::aio::knx::routing::busy_body_size> busy_packet {};
        REQUIRE(kmx::aio::knx::routing::encode_busy_packet(
                    busy_packet, kmx::aio::knx::routing::busy {.device_state = 0x01u, .wait_time_ms = 250u, .control_field = 0x0002u})
                    .has_value());
        const auto busy = kmx::aio::knx::routing::decode_busy_packet(busy_packet);
        REQUIRE(busy.has_value());
        CHECK(busy->device_state == 0x01u);
        CHECK(busy->wait_time_ms == 250u);
        CHECK(busy->control_field == 0x0002u);

        std::array<std::uint8_t, kmx::aio::knx::frame::communication_header_size + kmx::aio::knx::routing::lost_message_body_size>
            lost_packet {};
        REQUIRE(kmx::aio::knx::routing::encode_lost_message_packet(
                    lost_packet, kmx::aio::knx::routing::lost_message {.device_state = 0x03u, .count = 7u})
                    .has_value());
        const auto lost = kmx::aio::knx::routing::decode_lost_message_packet(lost_packet);
        REQUIRE(lost.has_value());
        CHECK(lost->device_state == 0x03u);
        CHECK(lost->count == 7u);
        CHECK(!kmx::aio::knx::routing::decode_busy_packet({busy_packet.data(), busy_packet.size() - 1u}).has_value());
    }
}
