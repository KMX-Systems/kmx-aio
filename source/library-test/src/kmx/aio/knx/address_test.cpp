/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/address.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_set>

namespace kmx::aio::test::knx::address_test
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

    TEST_CASE("knx group address reads the same bits in three styles", "[knx][address][unit]")
    {
        const auto address = group_address::make(1u, 2u, 3u);
        REQUIRE(address.has_value());

        CHECK(address->value() == 0x0A03u);
        CHECK(address->main_group() == 1u);
        CHECK(address->middle_group() == 2u);
        CHECK(address->sub_group() == 3u);
        CHECK(address->sub_group_two_level() == 515u);
        CHECK(address->to_string() == "1/2/3");
        CHECK(address->to_string(group_address_style::two_level) == "1/515");
        CHECK(address->to_string(group_address_style::free) == "2563");
        CHECK(!address->broadcast());
        CHECK(group_address {}.broadcast());
    }

    TEST_CASE("knx group address rejects components that do not fit", "[knx][address][unit]")
    {
        CHECK(group_address::make(31u, 7u, 255u).has_value());
        CHECK(group_address::make(32u, 0u, 0u).error() == error::invalid_address);
        CHECK(group_address::make(0u, 8u, 0u).error() == error::invalid_address);
        CHECK(group_address::make(0u, 0u, 256u).error() == error::invalid_address);
        CHECK(group_address::make(31u, 2047u).has_value());
        CHECK(group_address::make(0u, 2048u).error() == error::invalid_address);
    }

    TEST_CASE("knx group address prints the widest value in every style", "[knx][address][unit]")
    {
        // The free style prints the whole wire value as one number, so it needs five digits where every
        // level component needs at most three. A scratch buffer sized for a level component overruns here.
        constexpr group_address widest {0xFFFFu};

        CHECK(widest.to_string(group_address_style::free) == "65535");
        CHECK(widest.to_string(group_address_style::two_level) == "31/2047");
        CHECK(widest.to_string(group_address_style::three_level) == "31/7/255");

        CHECK(group_address::parse("65535").value() == widest);
        CHECK(group_address::parse("31/2047").value() == widest);
        CHECK(group_address::parse("31/7/255").value() == widest);
    }

    TEST_CASE("knx group address parses every style it prints", "[knx][address][unit]")
    {
        const auto three = group_address::parse("1/2/3");
        const auto two = group_address::parse("1/515");
        const auto free = group_address::parse("2563");

        REQUIRE(three.has_value());
        REQUIRE(two.has_value());
        REQUIRE(free.has_value());
        CHECK(*three == *two);
        CHECK(*three == *free);
    }

    TEST_CASE("knx group address rejects malformed text", "[knx][address][unit]")
    {
        for (const auto& text: {"", "/", "1/", "1/2/", "1/2/3/4", "32/0/0", "0/8/0", "0/0/256", "0/2048", "65536"})
            CHECK(!group_address::parse(text).has_value());
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

    TEST_CASE("knx addresses order and hash by wire value", "[knx][address][unit]")
    {
        STATIC_CHECK(individual_address {1u, 1u, 1u} < individual_address {1u, 1u, 2u});
        STATIC_CHECK(group_address {1u} < group_address {2u});

        const std::unordered_set<group_address> groups {group_address {1u}, group_address {2u}, group_address {1u}};
        CHECK(groups.size() == 2u);

        const std::unordered_set<individual_address> devices {individual_address {1u, 1u, 1u}, individual_address {1u, 1u, 1u}};
        CHECK(devices.size() == 1u);
    }
}
