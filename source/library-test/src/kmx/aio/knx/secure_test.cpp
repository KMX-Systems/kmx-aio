/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/datagram.hpp>
#include <kmx/aio/knx/secure.hpp>

#include <array>
#include <cstdint>
#include <system_error>
#include <vector>

namespace kmx::aio::test::knx::secure_test
{
    using namespace kmx::aio::knx;

    class reversible_provider final: public secure::provider
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

    TEST_CASE("knx secure packet encoder round-trips profile and sequence", "[knx][secure][integration]")
    {
        const secure::packet value {
            .selected = secure::profile::data_secure,
            .sequence = 0x0102030405060708ull,
            .payload = {0x11u, 0x22u, 0x33u, 0x44u},
        };
        std::array<std::uint8_t, 6u + 12u + 4u> wire {};
        REQUIRE(secure::encode_secure_packet(wire, value).has_value());

        const auto decoded = secure::decode_secure_packet(wire);
        REQUIRE(decoded.has_value());
        CHECK(decoded->selected == secure::profile::data_secure);
        CHECK(decoded->sequence == 0x0102030405060708ull);
        CHECK(decoded->payload == std::vector<std::uint8_t> {0x11u, 0x22u, 0x33u, 0x44u});
    }

    TEST_CASE("knx secure provider wraps payload in secure wire envelope", "[knx][secure][integration]")
    {
        reversible_provider provider {};
        const std::array<std::uint8_t, 3u> payload {0xAAu, 0xBBu, 0xCCu};

        const auto protected_packet = secure::protect_packet(
            provider, secure::profile::ip_secure, payload, 0x1234u);
        REQUIRE(protected_packet.has_value());

        const auto decoded = secure::decode_secure_packet(*protected_packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->selected == secure::profile::ip_secure);
        CHECK(decoded->sequence == 0x1234u);
        REQUIRE(decoded->payload.size() == payload.size() + 1u);
        CHECK(decoded->payload.back() == 0x34u);

        const auto restored = secure::unprotect_packet(
            provider, secure::profile::ip_secure, *protected_packet);
        REQUIRE(restored.has_value());
        CHECK(*restored == std::vector<std::uint8_t> {0xAAu, 0xBBu, 0xCCu});
    }

    TEST_CASE("knx secure unprotect rejects replayed packet sequence", "[knx][secure][unit]")
    {
        reversible_provider provider {};
        secure::replay_window_state replay {32u};
        const std::array<std::uint8_t, 2u> payload {0x10u, 0x20u};
        const auto protected_packet = secure::protect_packet(
            provider, secure::profile::data_secure, payload, 42u);
        REQUIRE(protected_packet.has_value());

        const auto first = secure::unprotect_packet(
            provider, secure::profile::data_secure, *protected_packet, &replay);
        REQUIRE(first.has_value());
        CHECK(*first == std::vector<std::uint8_t> {0x10u, 0x20u});

        const auto duplicate = secure::unprotect_packet(
            provider, secure::profile::data_secure, *protected_packet, &replay);
        REQUIRE(!duplicate.has_value());
        CHECK(duplicate.error() == make_error_code(error::sequence_error));
    }

    TEST_CASE("knx datagram dispatches secure packets", "[knx][secure][datagram][integration]")
    {
        const secure::packet secure_payload {
            .selected = secure::profile::data_secure,
            .sequence = 9u,
            .payload = {0x01u, 0x02u, 0x03u},
        };
        const datagram value {
            .service_type = secure::secure_service,
            .payload = secure_payload,
        };

        std::array<std::uint8_t, 6u + 12u + 3u> encoded {};
        REQUIRE(encode_datagram(encoded, value).has_value());

        const auto decoded = decode_datagram(encoded);
        REQUIRE(decoded.has_value());
        CHECK(decoded->service_type == secure::secure_service);
        REQUIRE(std::holds_alternative<secure::packet>(decoded->payload));
        const auto& decoded_secure = std::get<secure::packet>(decoded->payload);
        CHECK(decoded_secure.selected == secure::profile::data_secure);
        CHECK(decoded_secure.sequence == 9u);
        CHECK(decoded_secure.payload == std::vector<std::uint8_t> {0x01u, 0x02u, 0x03u});
    }
}
