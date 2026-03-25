/// @file aio/integration/pillar_2_integration_test.cpp
/// @brief Integration matrix for Pillar 2 TLS/ALPN API parity.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/tls/stream.hpp>
#include <kmx/aio/readiness/tls/stream.hpp>

#include <array>
#include <concepts>
#include <cstdint>
#include <expected>
#include <string_view>
#include <system_error>
#include <vector>

namespace kmx::aio::tls::test::integration
{
    template <typename Stream>
    concept alpn_api_surface = requires(Stream s, std::span<const std::uint8_t> p)
    {
        { s.set_alpn_protocols(p) } -> std::same_as<std::expected<void, std::error_code>>;
        { s.selected_alpn() } -> std::convertible_to<std::string_view>;
    };

    [[nodiscard]] static std::vector<std::uint8_t> encode_alpn_wire_format(const std::initializer_list<std::string_view> protocols)
    {
        std::vector<std::uint8_t> wire;
        for (const auto protocol : protocols)
        {
            wire.push_back(static_cast<std::uint8_t>(protocol.size()));
            wire.insert(wire.end(), protocol.begin(), protocol.end());
        }
        return wire;
    }

    TEST_CASE("Pillar 2: TLS ALPN API matrix parity", "[pillar2][integration][tls][alpn]")
    {
        STATIC_REQUIRE(alpn_api_surface<kmx::aio::readiness::tls::stream>);
        STATIC_REQUIRE(alpn_api_surface<kmx::aio::completion::tls::stream>);

        const auto wire = encode_alpn_wire_format({"h2", "http/1.1"});
        const std::array<std::uint8_t, 12> expected {
            2u, static_cast<std::uint8_t>('h'), static_cast<std::uint8_t>('2'),
            8u,
            static_cast<std::uint8_t>('h'),
            static_cast<std::uint8_t>('t'),
            static_cast<std::uint8_t>('t'),
            static_cast<std::uint8_t>('p'),
            static_cast<std::uint8_t>('/'),
            static_cast<std::uint8_t>('1'),
            static_cast<std::uint8_t>('.'),
            static_cast<std::uint8_t>('1'),
        };

        REQUIRE(wire.size() == expected.size());
        REQUIRE(std::equal(wire.begin(), wire.end(), expected.begin()));

        SECTION("Readiness stream rejects ALPN set without initialized SSL handle")
        {
            kmx::aio::readiness::tls::stream stream {};
            const auto result = stream.set_alpn_protocols(std::span<const std::uint8_t>(wire.data(), wire.size()));

            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == std::make_error_code(std::errc::invalid_argument));
        }

        SECTION("Completion stream rejects ALPN set without initialized SSL handle")
        {
            kmx::aio::completion::tls::stream stream {};
            const auto result = stream.set_alpn_protocols(std::span<const std::uint8_t>(wire.data(), wire.size()));

            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == std::make_error_code(std::errc::invalid_argument));
        }
    }
} // namespace kmx::aio::tls::test::integration
