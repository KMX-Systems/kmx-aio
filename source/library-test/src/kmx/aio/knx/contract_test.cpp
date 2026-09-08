/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#define KMX_AIO_EXPECTS(condition) static_cast<void>(condition)
#define KMX_AIO_ENSURES(condition) static_cast<void>(condition)

#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/contract.hpp>
#include <kmx/aio/knx/secure.hpp>
#include <kmx/aio/knx/routing.hpp>
#include <kmx/aio/test/knx/telegram.hpp>

namespace kmx::aio::test::knx::contract_test
{
    class reversible_provider final: public kmx::aio::knx::secure::provider
    {
    public:
        [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::error_code> protect(
            const std::span<const std::uint8_t> packet, const std::uint64_t sequence) noexcept override
        {
            std::vector<std::uint8_t> result(packet.begin(), packet.end());
            result.push_back(static_cast<std::uint8_t>(sequence & 0xFFu));
            return result;
        }

        [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::error_code> unprotect(
            const std::span<const std::uint8_t> packet, const std::uint64_t sequence) noexcept override
        {
            if (packet.empty() || (packet.back() != static_cast<std::uint8_t>(sequence & 0xFFu)))
                return std::unexpected(std::make_error_code(std::errc::protocol_error));
            return std::vector<std::uint8_t>(packet.begin(), packet.end() - 1);
        }
    };

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

    TEST_CASE("knx secure configuration validates profile and replay policy", "[knx][secure][unit]")
    {
        kmx::aio::knx::secure::configuration disabled {};
        CHECK(kmx::aio::knx::secure::validate(disabled).has_value());

        kmx::aio::knx::secure::configuration invalid_window {
            .selected = kmx::aio::knx::secure::profile::ip_secure,
            .replay = kmx::aio::knx::secure::replay_policy::accept_within_window,
            .replay_window = 0u,
        };
        CHECK(kmx::aio::knx::secure::validate(invalid_window).error() == kmx::aio::knx::error::invalid_configuration);

        kmx::aio::knx::secure::configuration valid {
            .selected = kmx::aio::knx::secure::profile::data_secure,
            .replay = kmx::aio::knx::secure::replay_policy::accept_within_window,
            .replay_window = 32u,
            .key = {1u},
        };
        CHECK(kmx::aio::knx::secure::validate(valid).has_value());
    }

    TEST_CASE("knx secure provider binds protection to sequence context", "[knx][secure][unit]")
    {
        reversible_provider provider {};
        const std::array<std::uint8_t, 3u> plain {1u, 2u, 3u};
        const auto protected_packet = provider.protect(plain, 7u);
        REQUIRE(protected_packet.has_value());
        CHECK(protected_packet->back() == 7u);

        const auto restored = provider.unprotect(*protected_packet, 7u);
        REQUIRE(restored.has_value());
        CHECK(*restored == std::vector<std::uint8_t> {1u, 2u, 3u});
        CHECK(!provider.unprotect(*protected_packet, 8u).has_value());
    }

    TEST_CASE("knx secure replay window rejects duplicates and stale sequences", "[knx][secure][unit]")
    {
        kmx::aio::knx::secure::replay_window_state window {4u};
        CHECK(window.accept(100u));
        CHECK(window.accept(102u));
        CHECK(window.accept(101u));
        CHECK(!window.accept(101u));
        CHECK(!window.accept(98u));
        CHECK(window.accept(103u));
        CHECK(window.highest() == 103u);
    }

    TEST_CASE("knx routing configuration validates multicast policy", "[knx][routing][unit]")
    {
        CHECK(kmx::aio::knx::routing::validate({}).has_value());
        CHECK(kmx::aio::knx::routing::validate(
            kmx::aio::knx::routing::multicast_configuration {.group = {192u, 0u, 2u, 1u}}).error() ==
              kmx::aio::knx::error::invalid_configuration);
        CHECK(kmx::aio::knx::routing::validate(
            kmx::aio::knx::routing::multicast_configuration {.port = 0u}).error() ==
              kmx::aio::knx::error::invalid_configuration);
    }

    TEST_CASE("knx routing indication round-trips cEMI", "[knx][routing][integration]")
    {
        std::array<std::uint8_t, 512u> packet {};
        const kmx::aio::knx::routing::indication value {3u, sample_cemi};
        REQUIRE(kmx::aio::knx::routing::encode_indication_packet(packet, value).has_value());
        const auto decoded = kmx::aio::knx::routing::decode_indication_packet(
            {packet.data(), 6u + 4u + sample_cemi.size()});
        REQUIRE(decoded.has_value());
        CHECK(decoded->channel_id == 3u);
        CHECK(decoded->cemi_bytes.size() == sample_cemi.size());
    }

    TEST_CASE("knx routing busy and lost-message controls round-trip", "[knx][routing][unit]")
    {
        std::array<std::uint8_t, 8u> busy_packet {};
        REQUIRE(kmx::aio::knx::routing::encode_busy_packet(
            busy_packet, kmx::aio::knx::routing::busy {.wait_time_ms = 250u}).has_value());
        const auto busy = kmx::aio::knx::routing::decode_busy_packet(busy_packet);
        REQUIRE(busy.has_value());
        CHECK(busy->wait_time_ms == 250u);

        std::array<std::uint8_t, 8u> lost_packet {};
        REQUIRE(kmx::aio::knx::routing::encode_lost_message_packet(
            lost_packet, kmx::aio::knx::routing::lost_message {.count = 7u}).has_value());
        const auto lost = kmx::aio::knx::routing::decode_lost_message_packet(lost_packet);
        REQUIRE(lost.has_value());
        CHECK(lost->count == 7u);
        CHECK(!kmx::aio::knx::routing::decode_busy_packet(
            {busy_packet.data(), busy_packet.size() - 1u}).has_value());
    }
}
