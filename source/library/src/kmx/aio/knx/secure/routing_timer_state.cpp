/// @file src/kmx/aio/knx/secure/routing_timer_state.cpp
/// @brief The compiled body of the KNX IP Secure routing timer: wrapper acceptance, duplicates and TIMER_NOTIFY.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/routing_timer_state.hpp>
#ifndef PCH
    #include <algorithm>
    #include <array>
    #include <limits>
    #include <span>
#endif

namespace kmx::aio::knx::secure
{
    routing_timer_state::routing_timer_state(const std::uint16_t latency_tolerance_ms, const serial_number_t& serial_number,
                                             const std::uint16_t duplicate_cache_entries, entropy_source& entropy) noexcept(false):
        entropy_(entropy),
        serial_number_(serial_number),
        latency_ms_(latency_tolerance_ms),
        sync_latency_ms_((static_cast<std::int64_t>(latency_tolerance_ms) + 5) / 10),
        cache_(std::max<std::size_t>(duplicate_cache_entries, 1u))
    {
    }

    std::uint64_t routing_timer_state::timer_value(const std::uint64_t now_ms) const noexcept
    {
        const auto value = static_cast<std::int64_t>(now_ms) + clock_difference_;
        return static_cast<std::uint64_t>(std::clamp<std::int64_t>(value, 0, static_cast<std::int64_t>(max_sequence)));
    }

    timer_notify_request routing_timer_state::begin_synchronisation(const std::uint64_t now_ms) noexcept
    {
        // xknx waits the longest follower update delay plus twice the latency tolerance: 3.3 s at 1000 ms.
        const auto wait = static_cast<std::int64_t>(keeper_update_notify_min_ms) + (12 * sync_latency_ms_) + (2 * latency_ms_);
        synchronised_ = false;
        synchronisation_tag_ = random_tag();
        synchronisation_deadline_ms_ = now_ms + static_cast<std::uint64_t>(wait);
        ++counters_.timer_notifications_sent;
        return {serial_number_, *synchronisation_tag_};
    }

    void routing_timer_state::finish_synchronisation(const std::uint64_t now_ms) noexcept
    {
        synchronisation_tag_.reset();
        synchronised_ = true;
        reschedule(now_ms, std::nullopt);
    }

    void routing_timer_state::advance(const std::int64_t local, const std::int64_t received) noexcept
    {
        if (received <= local)
            return;
        clock_difference_ += received - local;
        ++counters_.timer_adjustments;
    }

    void routing_timer_state::on_timer_notify(const std::uint64_t now_ms, const routing_frame_identity& notify) noexcept
    {
        // E11: the answer to this router's own request echoes its serial number and the tag it chose.
        const auto answers_request =
            synchronisation_tag_.has_value() && (notify.serial_number == serial_number_) && (notify.message_tag == *synchronisation_tag_);
        if (answers_request)
        {
            clock_difference_ = static_cast<std::int64_t>(notify.timer_value) - static_cast<std::int64_t>(now_ms);
            ++counters_.timer_adjustments;
            finish_synchronisation(now_ms);
            return;
        }

        const auto local = static_cast<std::int64_t>(timer_value(now_ms));
        const auto received = static_cast<std::int64_t>(notify.timer_value);
        if (received > (local - sync_latency_ms_)) // E1 and E2: another router keeps the time.
        {
            advance(local, received);
            timekeeper_ = false;
            reschedule(now_ms, std::nullopt);
        }
        else if ((received <= (local - latency_ms_)) && !scheduled_update_.has_value()) // E4; E3 changes nothing.
            reschedule(now_ms, timer_notify_request {notify.serial_number, notify.message_tag});
    }

    wrapper_verdict routing_timer_state::on_wrapper(const std::uint64_t now_ms, const routing_frame_identity& wrapper) noexcept
    {
        // Duplicates are dropped before the acceptance rule runs, so a replay changes no schedule either.
        if (!synchronised_ || remembered(wrapper))
        {
            ++(synchronised_ ? counters_.duplicates : counters_.replays);
            return synchronised_ ? wrapper_verdict::duplicate : wrapper_verdict::unsynchronised;
        }

        const auto local = static_cast<std::int64_t>(timer_value(now_ms));
        const auto received = static_cast<std::int64_t>(wrapper.timer_value);
        if (received <= (local - latency_ms_)) // E8
        {
            if (!scheduled_update_.has_value())
                reschedule(now_ms, timer_notify_request {wrapper.serial_number, wrapper.message_tag});
            ++counters_.replays;
            return wrapper_verdict::outdated;
        }

        if ((received > (local - sync_latency_ms_)) && !scheduled_update_.has_value()) // E5 and E6; E7 changes nothing.
            reschedule(now_ms, std::nullopt);
        advance(local, received);
        remember(wrapper);
        return wrapper_verdict::accepted;
    }

    std::uint64_t routing_timer_state::on_outgoing_wrapper(const std::uint64_t now_ms) noexcept
    {
        if (!scheduled_update_.has_value()) // E9
            reschedule(now_ms, std::nullopt);
        return timer_value(now_ms);
    }

    std::optional<timer_notify_request> routing_timer_state::take_due_notify(const std::uint64_t now_ms) noexcept
    {
        if (synchronisation_tag_.has_value() && (now_ms >= synchronisation_deadline_ms_))
        {
            // Nobody answered, so this router keeps the time from now on.
            timekeeper_ = true;
            finish_synchronisation(now_ms);
        }

        if (!notify_deadline_ms_.has_value() || (now_ms < *notify_deadline_ms_))
            return std::nullopt;

        // Not value_or: its argument is evaluated either way, and an update must not spend a random tag.
        const auto request = scheduled_update_.has_value() ? *scheduled_update_ : timer_notify_request {serial_number_, random_tag()};
        timekeeper_ = true;
        reschedule(now_ms, std::nullopt);
        ++counters_.timer_notifications_sent;
        return request;
    }

    std::uint64_t routing_timer_state::next_deadline_ms() const noexcept
    {
        const auto notify = notify_deadline_ms_.value_or(std::numeric_limits<std::uint64_t>::max());
        return synchronisation_tag_.has_value() ? std::min(notify, synchronisation_deadline_ms_) : notify;
    }

    void routing_timer_state::reset() noexcept
    {
        clock_difference_ = 0;
        synchronised_ = false;
        timekeeper_ = false;
        synchronisation_tag_.reset();
        synchronisation_deadline_ms_ = 0u;
        scheduled_update_.reset();
        notify_deadline_ms_.reset();
        cache_next_ = 0u;
        cache_count_ = 0u;
    }

    routing_timer_state::delay_bounds_t routing_timer_state::delay_bounds(const bool update) const noexcept
    {
        // A follower waits past the timekeeper's whole range, so a timekeeper that is alive always speaks first.
        const auto keeper_minimum = static_cast<std::int64_t>(update ? keeper_update_notify_min_ms : keeper_periodic_notify_min_ms);
        const auto keeper_maximum = keeper_minimum + ((update ? 1 : 3) * sync_latency_ms_);
        if (timekeeper_)
            return {keeper_minimum, keeper_maximum};
        return {keeper_maximum + sync_latency_ms_, keeper_maximum + (11 * sync_latency_ms_)};
    }

    void routing_timer_state::reschedule(const std::uint64_t now_ms, const std::optional<timer_notify_request> update) noexcept
    {
        const auto [minimum, maximum] = delay_bounds(update.has_value());
        scheduled_update_ = update;
        notify_deadline_ms_ = now_ms + random_between(minimum, maximum);
    }

    message_tag_t routing_timer_state::random_tag() noexcept
    {
        message_tag_t tag {};
        if (entropy_.fill(tag).has_value())
            return tag;
        // A tag is not a secret, only a name for a message, so a counter serves when the backend cannot supply one.
        ++fallback_tag_;
        return {static_cast<std::uint8_t>(fallback_tag_ >> 8u), static_cast<std::uint8_t>(fallback_tag_ & 0xFFu)};
    }

    std::uint64_t routing_timer_state::random_between(const std::int64_t minimum, const std::int64_t maximum) noexcept
    {
        std::array<std::uint8_t, 4u> octets {};
        if (!entropy_.fill(octets).has_value())
            return static_cast<std::uint64_t>(minimum);
        const auto draw = (std::uint64_t {octets[0u]} << 24u) | (std::uint64_t {octets[1u]} << 16u) | (std::uint64_t {octets[2u]} << 8u) |
                          std::uint64_t {octets[3u]};
        return static_cast<std::uint64_t>(minimum) + (draw % (static_cast<std::uint64_t>(maximum - minimum) + 1u));
    }

    void routing_timer_state::note_refused(const refusal reason) noexcept
    {
        switch (reason)
        {
            case refusal::authentication:
                ++counters_.authentication_failures;
                break;
            case refusal::unencrypted:
                ++counters_.unencrypted_refused;
                break;
            case refusal::service:
                ++counters_.refused_services;
                break;
        }
    }

    bool routing_timer_state::remembered(const routing_frame_identity& frame) const noexcept
    {
        const auto held = std::span {cache_}.first(cache_count_);
        return std::ranges::find(held, frame) != held.end();
    }

    void routing_timer_state::remember(const routing_frame_identity& frame) noexcept
    {
        cache_[cache_next_] = frame;
        cache_next_ = (cache_next_ + 1u) % cache_.size();
        cache_count_ = std::min(cache_count_ + 1u, cache_.size());
    }
}
