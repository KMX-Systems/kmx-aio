/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/secure/routing_timer_state.hpp>
#include <kmx/aio/test/knx/secure_vectors.hpp>

#include <cstdint>
#include <memory>

namespace kmx::aio::test::knx::secure::routing_timer_state_test
{
    namespace ks = kmx::aio::knx::secure;
    using verdict = ks::wrapper_verdict;

    namespace detail
    {
        using scripted_entropy = kmx::aio::test::knx::secure_vectors::scripted_entropy;

        inline constexpr ks::serial_number_t own_serial {0x00u, 0x00u, 0x78u, 0x6Bu, 0x6Eu, 0x78u};
        inline constexpr ks::serial_number_t other_serial {0x00u, 0xFAu, 0x12u, 0x34u, 0x56u, 0x78u};
        inline constexpr std::uint64_t one_hour_ms = 3'600'000u;
        /// @brief At a 1000 ms latency tolerance: the soonest periodic notify of a follower, and of an update.
        inline constexpr std::uint64_t follower_periodic_min_ms = 10'400u;
        inline constexpr std::uint64_t follower_update_min_ms = 300u;

        [[nodiscard]] ks::routing_frame_identity frame(const std::uint64_t timer_value, const ks::serial_number_t& serial = other_serial,
                                                       const std::uint16_t tag = 0x1234u) noexcept
        {
            return {timer_value, serial, {static_cast<std::uint8_t>(tag >> 8u), static_cast<std::uint8_t>(tag & 0xFFu)}};
        }

        /// @brief A timer that synchronised to one hour at time zero, from the answer to its own request.
        [[nodiscard]] std::unique_ptr<ks::routing_timer_state> synchronised_timer(scripted_entropy& entropy) noexcept(false)
        {
            auto timer = std::make_unique<ks::routing_timer_state>(1000u, own_serial, 256u, entropy);
            entropy.tag(0xABCDu);
            (void) timer->begin_synchronisation(0u);
            timer->on_timer_notify(0u, frame(one_hour_ms, own_serial, 0xABCDu));
            REQUIRE(timer->synchronised());
            return timer;
        }

        /// @brief A timer that synchronised by timing out, so it keeps the time, at 3300 ms with its timer there too.
        [[nodiscard]] std::unique_ptr<ks::routing_timer_state> timekeeper(scripted_entropy& entropy,
                                                                          const std::uint16_t cache = 256u) noexcept(false)
        {
            auto timer = std::make_unique<ks::routing_timer_state>(1000u, own_serial, cache, entropy);
            (void) timer->begin_synchronisation(0u);
            REQUIRE(!timer->take_due_notify(3'300u).has_value());
            REQUIRE(timer->timekeeper());
            return timer;
        }
    } // namespace detail

    TEST_CASE("knx secure routing timer synchronises from the answer to its own request", "[knx][secure][routing][unit]")
    {
        detail::scripted_entropy entropy {};
        ks::routing_timer_state timer {1000u, detail::own_serial, 256u, entropy};
        CHECK(timer.sync_latency_tolerance_ms() == 100u);
        entropy.tag(0xABCDu);
        const auto request = timer.begin_synchronisation(0u);
        CHECK(request.serial_number == detail::own_serial);
        CHECK(request.message_tag == ks::message_tag_t {0xABu, 0xCDu});
        CHECK(timer.synchronising());
        CHECK(timer.next_deadline_ms() == 3'300u);

        // Another router's serial with the right tag, or this router's serial with another tag, answers nothing -
        // although a newer timer still pulls the local one forward.
        timer.on_timer_notify(10u, detail::frame(5'000u, detail::other_serial, 0xABCDu));
        timer.on_timer_notify(10u, detail::frame(6'000u, detail::own_serial, 0xABCEu));
        CHECK(timer.timer_value(10u) == 6'000u);
        CHECK(timer.synchronising());
        CHECK(!timer.synchronised());

        timer.on_timer_notify(20u, detail::frame(detail::one_hour_ms, detail::own_serial, 0xABCDu));
        CHECK(timer.synchronised());
        CHECK(!timer.synchronising());
        CHECK(!timer.timekeeper());
        CHECK(timer.timer_value(20u) == detail::one_hour_ms);
        CHECK(timer.next_deadline_ms() == 20u + detail::follower_periodic_min_ms);
    }

    TEST_CASE("knx secure routing timer keeps the time when its request goes unanswered", "[knx][secure][routing][unit]")
    {
        detail::scripted_entropy entropy {};
        ks::routing_timer_state timer {1000u, detail::own_serial, 256u, entropy};
        (void) timer.begin_synchronisation(0u);
        CHECK(!timer.take_due_notify(3'299u).has_value());
        CHECK(!timer.synchronised());
        CHECK(!timer.take_due_notify(3'300u).has_value());
        CHECK(timer.synchronised());
        CHECK(timer.timekeeper());
        CHECK(timer.next_deadline_ms() == 3'300u + ks::keeper_periodic_notify_min_ms);

        const auto periodic = timer.take_due_notify(13'300u);
        REQUIRE(periodic.has_value());
        CHECK(periodic->serial_number == detail::own_serial);
        CHECK(timer.counters().timer_notifications_sent == 2u);
        CHECK(timer.next_deadline_ms() == 13'300u + ks::keeper_periodic_notify_min_ms);
    }

    TEST_CASE("knx secure routing timer accepts wrappers inside the latency window", "[knx][secure][routing][unit]")
    {
        detail::scripted_entropy entropy {};
        const auto timer = detail::synchronised_timer(entropy);
        std::uint64_t now = 5u;
        auto local = timer->timer_value(now);

        // E5: a newer timer pulls the local one forward and restarts the periodic notify.
        CHECK(timer->on_wrapper(now, detail::frame(local + 500u, detail::other_serial, 1u)) == verdict::accepted);
        CHECK(timer->timer_value(now) == local + 500u);
        CHECK(timer->next_deadline_ms() == now + detail::follower_periodic_min_ms);
        CHECK(timer->counters().timer_adjustments == 2u);

        // E6: behind by less than the sync latency tolerance also restarts it.
        now = 6u;
        local = timer->timer_value(now);
        CHECK(timer->on_wrapper(now, detail::frame(local - 99u, detail::other_serial, 2u)) == verdict::accepted);
        CHECK(timer->next_deadline_ms() == now + detail::follower_periodic_min_ms);
        CHECK(timer->timer_value(now) == local);

        // E7: behind by the sync latency tolerance or more, but inside the latency tolerance: accepted, nothing restarted.
        now = 7u;
        local = timer->timer_value(now);
        CHECK(timer->on_wrapper(now, detail::frame(local - 100u, detail::other_serial, 3u)) == verdict::accepted);
        CHECK(timer->on_wrapper(now, detail::frame(local - 999u, detail::other_serial, 4u)) == verdict::accepted);
        CHECK(timer->next_deadline_ms() == 6u + detail::follower_periodic_min_ms);

        // E8: behind by the latency tolerance or more: dropped, and an update is scheduled for that sender.
        CHECK(timer->on_wrapper(now, detail::frame(local - 1000u, detail::other_serial, 5u)) == verdict::outdated);
        CHECK(timer->update_scheduled());
        CHECK(timer->next_deadline_ms() == now + detail::follower_update_min_ms);
        CHECK(timer->counters().replays == 1u);
        CHECK(timer->on_wrapper(now + 1u, detail::frame(local - 1000u, detail::other_serial, 6u)) == verdict::outdated);
        CHECK(timer->next_deadline_ms() == now + detail::follower_update_min_ms);

        const auto update = timer->take_due_notify(now + detail::follower_update_min_ms);
        REQUIRE(update.has_value());
        CHECK(update->serial_number == detail::other_serial);
        CHECK(update->message_tag == ks::message_tag_t {0x00u, 0x05u});
        CHECK(timer->timekeeper());
        CHECK(!timer->update_scheduled());
    }

    TEST_CASE("knx secure routing timer drops exact duplicates without touching its schedule", "[knx][secure][routing][unit]")
    {
        detail::scripted_entropy entropy {};
        const auto timer = detail::synchronised_timer(entropy);
        const auto local = timer->timer_value(3u);
        const auto original = detail::frame(local, detail::other_serial, 0x0042u);
        CHECK(timer->on_wrapper(3u, original) == verdict::accepted);
        const auto deadline = timer->next_deadline_ms();

        CHECK(timer->on_wrapper(9u, original) == verdict::duplicate);
        CHECK(timer->next_deadline_ms() == deadline);
        CHECK(timer->counters().duplicates == 1u);
        CHECK(timer->counters().replays == 0u);

        // A frame differing in any one field is another frame.
        CHECK(timer->on_wrapper(9u, detail::frame(local, detail::other_serial, 0x0043u)) == verdict::accepted);
        CHECK(timer->on_wrapper(9u, detail::frame(local, detail::own_serial, 0x0042u)) == verdict::accepted);
        CHECK(timer->on_wrapper(9u, detail::frame(local + 1u, detail::other_serial, 0x0042u)) == verdict::accepted);
    }

    TEST_CASE("knx secure routing timer remembers a bounded number of frames", "[knx][secure][routing][unit]")
    {
        detail::scripted_entropy entropy {};
        const auto timer = detail::timekeeper(entropy, 2u);
        const auto local = timer->timer_value(3'300u);
        for (std::uint16_t tag = 1u; tag <= 3u; ++tag)
            CHECK(timer->on_wrapper(3'300u, detail::frame(local, detail::other_serial, tag)) == verdict::accepted);
        CHECK(timer->on_wrapper(3'300u, detail::frame(local, detail::other_serial, 3u)) == verdict::duplicate);
        // The oldest entry made room for the newest, so its replay is no longer recognised.
        CHECK(timer->on_wrapper(3'300u, detail::frame(local, detail::other_serial, 1u)) == verdict::accepted);
    }

    TEST_CASE("knx secure routing timer refuses wrappers before it synchronises", "[knx][secure][routing][unit]")
    {
        detail::scripted_entropy entropy {};
        ks::routing_timer_state timer {1000u, detail::own_serial, 256u, entropy};
        CHECK(timer.on_wrapper(0u, detail::frame(0u)) == verdict::unsynchronised);
        (void) timer.begin_synchronisation(0u);
        CHECK(timer.on_wrapper(0u, detail::frame(0u)) == verdict::unsynchronised);
        CHECK(timer.counters().replays == 2u);
    }

    TEST_CASE("knx secure routing timer follows newer notifications and answers outdated ones", "[knx][secure][routing][unit]")
    {
        detail::scripted_entropy entropy {};
        const auto timer = detail::timekeeper(entropy);
        const auto local = timer->timer_value(3'300u);

        // E1: a newer timer makes this router a follower.
        timer->on_timer_notify(3'300u, detail::frame(local + 250u, detail::other_serial, 7u));
        CHECK(!timer->timekeeper());
        CHECK(timer->timer_value(3'300u) == local + 250u);

        // E3: inside the latency tolerance but past the sync tolerance: nothing changes.
        const auto deadline = timer->next_deadline_ms();
        timer->on_timer_notify(3'310u, detail::frame(local + 250u + 10u - 500u, detail::other_serial, 8u));
        CHECK(timer->next_deadline_ms() == deadline);
        CHECK(!timer->update_scheduled());

        // E4: outdated: an update for that router is scheduled.
        timer->on_timer_notify(3'310u, detail::frame(0u, detail::other_serial, 9u));
        CHECK(timer->update_scheduled());
        const auto update = timer->take_due_notify(timer->next_deadline_ms());
        REQUIRE(update.has_value());
        CHECK(update->message_tag == ks::message_tag_t {0x00u, 0x09u});
        CHECK(update->serial_number == detail::other_serial);
    }

    TEST_CASE("knx secure routing timer stamps outgoing wrappers", "[knx][secure][routing][unit]")
    {
        detail::scripted_entropy entropy {};
        const auto timer = detail::synchronised_timer(entropy);

        // E9: sending restarts the periodic notify...
        CHECK(timer->on_outgoing_wrapper(50u) == detail::one_hour_ms + 50u);
        CHECK(timer->next_deadline_ms() == 50u + detail::follower_periodic_min_ms);

        // ...but never postpones an update that is due.
        CHECK(timer->on_wrapper(60u, detail::frame(0u)) == verdict::outdated);
        (void) timer->on_outgoing_wrapper(70u);
        CHECK(timer->next_deadline_ms() == 60u + detail::follower_update_min_ms);
    }

    TEST_CASE("knx secure routing timer draws its delays inside xknx's ranges", "[knx][secure][routing][unit]")
    {
        detail::scripted_entropy entropy {};
        ks::routing_timer_state timer {1000u, detail::own_serial, 256u, entropy};
        (void) timer.begin_synchronisation(0u);

        // A timekeeper's periodic notify: 10 000 to 10 300 ms. A draw of 300 is the longest; 301 wraps to the shortest.
        entropy.delay(300u);
        CHECK(!timer.take_due_notify(3'300u).has_value());
        CHECK(timer.next_deadline_ms() == 3'300u + 10'300u);
        entropy.tag(0x0001u);
        entropy.delay(301u);
        REQUIRE(timer.take_due_notify(13'600u).has_value());
        CHECK(timer.next_deadline_ms() == 13'600u + 10'000u);

        // A follower's update notify: 300 to 1300 ms.
        timer.on_timer_notify(13'600u, detail::frame(timer.timer_value(13'600u) + 1u));
        CHECK(!timer.timekeeper());
        entropy.delay(1000u);
        timer.on_timer_notify(13'600u, detail::frame(0u, detail::other_serial, 0x0002u));
        CHECK(timer.next_deadline_ms() == 13'600u + 1'300u);
    }

    TEST_CASE("knx secure routing timer keeps working when its entropy source fails", "[knx][secure][routing][unit]")
    {
        detail::scripted_entropy entropy {};
        entropy.failing = true;
        ks::routing_timer_state timer {1000u, detail::own_serial, 256u, entropy};
        // A tag names a message and is no secret, so a counter stands in; delays fall back to their shortest.
        CHECK(timer.begin_synchronisation(0u).message_tag == ks::message_tag_t {0x00u, 0x01u});
        CHECK(!timer.take_due_notify(3'300u).has_value());
        CHECK(timer.next_deadline_ms() == 3'300u + ks::keeper_periodic_notify_min_ms);
        const auto periodic = timer.take_due_notify(13'300u);
        REQUIRE(periodic.has_value());
        CHECK(periodic->message_tag == ks::message_tag_t {0x00u, 0x02u});
    }
}
