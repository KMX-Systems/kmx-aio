/// @file src/kmx/aio/knx/individual_address_test.cpp
/// @brief Unit tests for KNX individual addresses: components, text, caller-supplied buffers, ordering and hashing.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/individual_address.hpp>
#ifndef PCH
    #include <kmx/aio/knx/error.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <array>
    #include <cstdint>
    #include <string>
    #include <unordered_set>
#endif

namespace kmx::aio::test::knx::individual_address_test
{
    using namespace kmx::aio::knx;

    TEST_CASE("knx individual address splits its wire value into area line and device", "[knx][address][unit]")
    {
        constexpr individual_address address {1u, 2u, 3u};

        STATIC_CHECK(address.value() == 0x1203u);
        STATIC_CHECK(address.area() == 1u);
        STATIC_CHECK(address.line() == 2u);
        STATIC_CHECK(address.device() == 3u);
        STATIC_CHECK(!address.unset());
        STATIC_CHECK(individual_address {}.unset());
        CHECK(address.to_string() == "1.2.3");
    }

    TEST_CASE("knx individual address rejects components that do not fit", "[knx][address][unit]")
    {
        CHECK(individual_address::make(15u, 15u, 255u).has_value());
        CHECK(individual_address::make(16u, 0u, 0u).error() == error::invalid_address);
        CHECK(individual_address::make(0u, 16u, 0u).error() == error::invalid_address);
        CHECK(individual_address::make(0u, 0u, 256u).error() == error::invalid_address);
    }

    TEST_CASE("knx individual address text round-trips", "[knx][address][unit]")
    {
        for (const auto& text: {"0.0.0", "1.1.1", "15.15.255", "10.7.128"})
        {
            const auto parsed = individual_address::parse(text);
            REQUIRE(parsed.has_value());
            CHECK(parsed->to_string() == text);
        }
    }

    TEST_CASE("knx individual address rejects malformed text", "[knx][address][unit]")
    {
        for (const auto& text: {"", "1", "1.1", "1.1.1.", "1.1.1.1", "1..1", "16.0.0", "1.1.256", "a.b.c", " 1.1.1"})
            CHECK(!individual_address::parse(text).has_value());
    }

    TEST_CASE("knx addresses format into a caller-supplied buffer", "[knx][address][unit]")
    {
        std::array<char, 9u> text {};
        const auto written = individual_address {15u, 15u, 255u}.format(text);
        REQUIRE(written.has_value());
        CHECK(std::string(text.data(), *written) == "15.15.255");

        std::array<char, 8u> too_small {};
        CHECK(individual_address {}.format(too_small).error() == error::invalid_length);
    }

    TEST_CASE("knx individual address orders and hashes by wire value", "[knx][address][unit]")
    {
        STATIC_CHECK(individual_address {1u, 1u, 1u} < individual_address {1u, 1u, 2u});

        const std::unordered_set<individual_address> devices {individual_address {1u, 1u, 1u}, individual_address {1u, 1u, 1u}};
        CHECK(devices.size() == 1u);
    }
}
