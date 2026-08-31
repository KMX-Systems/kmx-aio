/// @file aio/basic_channel.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/basic_channel.hpp>

namespace kmx::aio
{
    void basic_channel::wait_until_can_send() noexcept
    {
        while (true)
        {
            const auto head = head_.load(std::memory_order_acquire);
            const auto tail_snapshot = tail_.load(std::memory_order_acquire);
            const auto occ = (head - tail_snapshot) & mask_;
            const auto low = low_watermark_.load(std::memory_order_acquire);
            const auto high = high_watermark_.load(std::memory_order_acquire);
            const bool throttled = compute_throttled(occ, low, high, throttled_.load(std::memory_order_acquire));

            if (!throttled && (occ < usable_capacity()))
                return;

            if (throttled)
                throttled_.wait(true, std::memory_order_relaxed);
            else
                tail_.wait(tail_snapshot, std::memory_order_relaxed);
        }
    }

    void basic_channel::set_backpressure(const channel_backpressure_config& cfg) noexcept
    {
        const auto usable_slots = usable_capacity();
        std::size_t high = cfg.high_watermark;
        if (high == 0u)
            high = 1u;

        if (high > usable_slots)
            high = usable_slots;

        std::size_t low = cfg.low_watermark;
        if (low > high)
            low = high;

        low_watermark_.store(low, std::memory_order_release);
        high_watermark_.store(high, std::memory_order_release);

        const auto occ = occupancy();
        const bool current_throttled = throttled_.load(std::memory_order_acquire);
        const bool next_throttled = compute_throttled(occ, low, high, current_throttled);
        throttled_.store(next_throttled, std::memory_order_release);
        if (current_throttled && !next_throttled)
            throttled_.notify_all();
    }

    std::size_t basic_channel::producer_credits() const noexcept
    {
        const auto occ = occupancy();
        const auto high = high_watermark_.load(std::memory_order_acquire);
        return (occ >= high) ? 0u : (high - occ);
    }

    bool basic_channel::can_send() const noexcept
    {
        const auto occ = occupancy();
        const auto low = low_watermark_.load(std::memory_order_acquire);
        const auto high = high_watermark_.load(std::memory_order_acquire);
        const bool throttled = compute_throttled(occ, low, high, throttled_.load(std::memory_order_acquire));

        if (throttled)
            return false;

        return occ < usable_capacity();
    }

    std::size_t basic_channel::occupancy() const noexcept
    {
        const auto head = head_.load(std::memory_order_acquire);
        const auto tail = tail_.load(std::memory_order_acquire);
        return (head - tail) & mask_;
    }

    bool basic_channel::acquire_push_slot(push_slot& slot) noexcept
    {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto tail = tail_.load(std::memory_order_acquire);
        const auto next_head = (head + 1u) & mask_;

        // Full when the next write position equals the current read position
        if (next_head == tail)
            return false;

        const auto occ = (head - tail) & mask_;
        const auto low = low_watermark_.load(std::memory_order_acquire);
        const auto high = high_watermark_.load(std::memory_order_acquire);
        const bool current_throttled = throttled_.load(std::memory_order_acquire);

        const bool throttled_before_push = compute_throttled(occ, low, high, current_throttled);
        // LCOV_EXCL_START
        // A repair for a race, and reachable only through one. acquire_push_slot, publish_pop and
        // set_backpressure each leave the flag agreeing with the occupancy they just observed, so a
        // single-threaded caller never finds them apart on entry. They can only disagree when a
        // consumer pops, or a watermark moves, between the occupancy read above and the flag read
        // beside it - and then the flag is stale and this store is what corrects it before the
        // decision below is taken on it.
        if (throttled_before_push != current_throttled)
            throttled_.store(throttled_before_push, std::memory_order_release);
        // LCOV_EXCL_STOP

        if (throttled_before_push)
            return false;

        slot = push_slot {head, occ, low, high};
        return true;
    }

    void basic_channel::publish_push(const push_slot& slot) noexcept
    {
        head_.store((slot.index + 1u) & mask_, std::memory_order_release);

        // acquire_push_slot() only succeeds when the producer was not throttled
        const bool throttled_after_push = compute_throttled(slot.occupancy + 1u, slot.low, slot.high, false);
        if (throttled_after_push)
            throttled_.store(true, std::memory_order_release);
    }

    bool basic_channel::acquire_pop_slot(pop_slot& slot) noexcept
    {
        const auto tail = tail_.load(std::memory_order_relaxed);
        const auto head = head_.load(std::memory_order_acquire);

        // Empty when read position equals the write position
        if (tail == head)
            return false;

        slot = pop_slot {tail, head};
        return true;
    }

    void basic_channel::publish_pop(const pop_slot& slot) noexcept
    {
        const auto next_tail = (slot.index + 1u) & mask_;
        tail_.store(next_tail, std::memory_order_release);
        tail_.notify_all();

        const auto occ_after_pop = (slot.head - next_tail) & mask_;
        const auto low = low_watermark_.load(std::memory_order_acquire);
        // Read before the exchange, and only exchange when the flag is actually set. The exchange
        // is a locked read-modify-write the processor pays for whether or not it changes anything,
        // and the common case on this path - a consumer keeping up, so the ring sits well below the
        // low watermark - is clearing a flag that was never raised. The plain load costs nothing on
        // the same cache line the pop has already touched, and the exchange still settles any race
        // with a producer raising the flag, because it is what publishes the clear.
        if ((occ_after_pop <= low) && throttled_.load(std::memory_order_acquire))
        {
            const bool was_throttled = throttled_.exchange(false, std::memory_order_acq_rel);
            if (was_throttled)
                throttled_.notify_all();
        }
    }

    std::size_t basic_channel::next_power_of_two(std::size_t n) noexcept
    {
        if (n < 2u)
            return 2u;

        --n;
        n |= n >> 1u;
        n |= n >> 2u;
        n |= n >> 4u;
        n |= n >> 8u;
        n |= n >> 16u;
        n |= n >> 32u;
        return n + 1u;
    }

    bool basic_channel::compute_throttled(const std::size_t occupancy, const std::size_t low, const std::size_t high,
                                          const bool current) noexcept
    {
        if (occupancy <= low)
            return false;
        if (occupancy >= high)
            return true;
        return current;
    }

} // namespace kmx::aio
