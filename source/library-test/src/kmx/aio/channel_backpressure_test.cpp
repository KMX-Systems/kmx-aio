/// @file aio/channel_backpressure_test.cpp
/// @brief Unit tests for channel backpressure watermark and credit behavior.

#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/channel.hpp>

TEST_CASE("channel occupancy tracks push/pop", "[channel][backpressure]")
{
    kmx::aio::channel<int> ch(8u);

    REQUIRE(ch.occupancy() == 0u);
    REQUIRE(ch.can_send());

    REQUIRE(ch.try_push(1));
    REQUIRE(ch.try_push(2));
    REQUIRE(ch.occupancy() == 2u);

    auto v = ch.try_pop();
    REQUIRE(v.has_value());
    REQUIRE(*v == 1);
    REQUIRE(ch.occupancy() == 1u);
}

TEST_CASE("channel can_send throttles at high watermark", "[channel][backpressure]")
{
    kmx::aio::channel<int> ch(16u);
    ch.set_backpressure({.low_watermark = 2u, .high_watermark = 4u});

    REQUIRE(ch.can_send());
    REQUIRE(ch.try_push(1));
    REQUIRE(ch.try_push(2));
    REQUIRE(ch.try_push(3));
    REQUIRE(ch.try_push(4));

    REQUIRE(ch.occupancy() == 4u);
    REQUIRE_FALSE(ch.can_send());
    REQUIRE_FALSE(ch.try_push(5));
}

TEST_CASE("channel hysteresis releases throttle at low watermark", "[channel][backpressure]")
{
    kmx::aio::channel<int> ch(16u);
    ch.set_backpressure({.low_watermark = 2u, .high_watermark = 4u});

    REQUIRE(ch.try_push(10));
    REQUIRE(ch.try_push(11));
    REQUIRE(ch.try_push(12));
    REQUIRE(ch.try_push(13));
    REQUIRE_FALSE(ch.can_send());

    auto p1 = ch.try_pop();
    REQUIRE(p1.has_value());
    REQUIRE_FALSE(ch.can_send());

    auto p2 = ch.try_pop();
    REQUIRE(p2.has_value());
    REQUIRE(ch.occupancy() == 2u);
    REQUIRE(ch.can_send());
    REQUIRE(ch.try_push(14));
}

TEST_CASE("channel producer credits follow occupancy", "[channel][backpressure]")
{
    kmx::aio::channel<int> ch(16u);
    ch.set_backpressure({.low_watermark = 1u, .high_watermark = 5u});

    REQUIRE(ch.producer_credits() == 5u);

    REQUIRE(ch.try_push(1));
    REQUIRE(ch.try_push(2));
    REQUIRE(ch.producer_credits() == 3u);

    REQUIRE(ch.try_push(3));
    REQUIRE(ch.try_push(4));
    REQUIRE(ch.try_push(5));
    REQUIRE(ch.producer_credits() == 0u);
    REQUIRE_FALSE(ch.can_send());
}

TEST_CASE("channel backpressure config clamps to usable capacity", "[channel][backpressure]")
{
    kmx::aio::channel<int> ch(4u);
    ch.set_backpressure({.low_watermark = 100u, .high_watermark = 100u});

    REQUIRE(ch.capacity() == 4u);

    REQUIRE(ch.try_push(1));
    REQUIRE(ch.try_push(2));
    REQUIRE(ch.try_push(3));

    REQUIRE(ch.occupancy() == 3u);
    REQUIRE_FALSE(ch.can_send());
    REQUIRE_FALSE(ch.try_push(4));
}

TEST_CASE("channel default backpressure remains non-blocking below high", "[channel][backpressure]")
{
    kmx::aio::channel<int> ch(8u);

    REQUIRE(ch.try_push(1));
    REQUIRE(ch.try_push(2));
    REQUIRE(ch.try_push(3));
    REQUIRE(ch.can_send());

    auto v1 = ch.try_pop();
    REQUIRE(v1.has_value());
    REQUIRE(ch.can_send());
}
