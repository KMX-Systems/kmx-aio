/// @file avb/gptp/servo.cpp
/// @brief PI clock servo for gPTP (IEEE 802.1AS) timestamp adjustment via clock_adjtime().
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <kmx/aio/avb/gptp/servo.hpp>

namespace kmx::aio::avb::gptp
{
    void pi_servo::update(std::int64_t offset_ns, std::int64_t path_delay_ns) noexcept
    {
        // Apply path delay compensation
        const std::int64_t corrected = offset_ns - path_delay_ns;

        // On the first call, step the clock if offset is large (> 1 ms)
        if (!initialized_)
        {
            static constexpr std::int64_t initial_step_threshold_ns {1'000'000LL};
            if ((corrected > initial_step_threshold_ns) || (corrected < -initial_step_threshold_ns))
                step_clock(-corrected);

            initialized_ = true;
            integral_ = 0.0;
            last_offset_ = corrected;
            return;
        }

        integral_ += static_cast<double>(corrected) * ki_;

        const double adj_ns_per_s = static_cast<double>(corrected) * kp_ + integral_;

        // Convert to ppb (parts-per-billion) for ADJ_FREQUENCY
        // ADJ_FREQUENCY unit = 2^-16 ppm = 2^-16 * 1e-6 ns/s per tick
        // kernel adj = adj_ns_per_s * 65536 / 1000
        static constexpr double frequency_adjust_scale {65.536};
        const std::int64_t freq_adj = static_cast<std::int64_t>(adj_ns_per_s * frequency_adjust_scale);

        ::timex tx {};
        tx.modes = ADJ_FREQUENCY;
        tx.freq = freq_adj;
        ::clock_adjtime(CLOCK_TAI, &tx);

        last_offset_ = corrected;

        // Synced when offset < 1 us
        static constexpr std::int64_t sync_lock_threshold_ns {1000LL};
        synced_ = (corrected > -sync_lock_threshold_ns && corrected < sync_lock_threshold_ns);
    }

    void pi_servo::reset() noexcept
    {
        initialized_ = {};
        synced_ = {};
        integral_ = {};
        last_offset_ = {};
    }

    void pi_servo::step_clock(std::int64_t delta_ns) noexcept
    {
        static constexpr std::int64_t nanoseconds_per_second {1'000'000'000LL};
        ::timex tx {};
        tx.modes = ADJ_SETOFFSET | ADJ_NANO;
        tx.time.tv_sec = delta_ns / nanoseconds_per_second;
        tx.time.tv_usec = delta_ns % nanoseconds_per_second; // tv_usec holds ns with ADJ_NANO
        ::clock_adjtime(CLOCK_TAI, &tx);
    }
} // namespace kmx::aio::avb::gptp