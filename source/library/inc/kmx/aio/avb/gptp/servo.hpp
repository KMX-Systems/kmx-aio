/// @file avb/gptp/servo.hpp
/// @brief PI clock servo for gPTP (IEEE 802.1AS) timestamp adjustment via clock_adjtime().
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <cstdint>
#include <ctime>

#include <sys/timex.h>

#include <kmx/aio/avb/avb_types.hpp>

namespace kmx::aio::avb::gptp
{
    /// @brief Proportional-Integral servo for adjusting CLOCK_TAI to match the gPTP master.
    ///
    /// The servo receives an offset measurement (local TAI − master TAI, in ns) on each
    /// Sync+Follow_Up pair and adjusts the kernel's CLOCK_TAI frequency via ADJ_FREQUENCY.
    /// Once the accumulated offset drops below a threshold, the clock is considered "synced".
    class pi_servo
    {
    public:
        pi_servo() noexcept = default;

        /// @brief Feed a new offset measurement (ns) and path delay (ns) to the servo.
        /// @param offset_ns     Signed offset: local_time - master_time (nanoseconds).
        /// @param path_delay_ns One-way peer delay (nanoseconds).
        void update(std::int64_t offset_ns, std::int64_t path_delay_ns) noexcept
        {
            // Apply path delay compensation
            const std::int64_t corrected = offset_ns - path_delay_ns;

            // On the first call, step the clock if offset is large (> 1 ms)
            if (!initialized_)
            {
                if (corrected > 1'000'000LL || corrected < -1'000'000LL)
                    step_clock(-corrected);
                initialized_ = true;
                integral_    = 0.0;
                last_offset_ = corrected;
                return;
            }

            integral_ += static_cast<double>(corrected) * ki_;

            const double adj_ns_per_s = static_cast<double>(corrected) * kp_ + integral_;

            // Convert to ppb (parts-per-billion) for ADJ_FREQUENCY
            // ADJ_FREQUENCY unit = 2^-16 ppm = 2^-16 * 1e-6 ns/s per tick
            // kernel adj = adj_ns_per_s * 65536 / 1000
            const std::int64_t freq_adj = static_cast<std::int64_t>(adj_ns_per_s * 65.536);

            ::timex tx {};
            tx.modes = ADJ_FREQUENCY;
            tx.freq  = freq_adj;
            ::clock_adjtime(CLOCK_TAI, &tx);

            last_offset_ = corrected;

            // Synced when offset < 1 µs
            synced_ = (corrected > -1000LL && corrected < 1000LL);
        }

        /// @brief Returns true if the servo considers the clock synchronized.
        [[nodiscard]] bool is_synced() const noexcept { return synced_; }

        /// @brief Current filtered offset in nanoseconds.
        [[nodiscard]] std::int64_t last_offset() const noexcept { return last_offset_; }

        /// @brief Reset servo state (e.g., after master change).
        void reset() noexcept
        {
            initialized_ = false;
            synced_      = false;
            integral_    = 0.0;
            last_offset_ = 0;
        }

    private:
        void step_clock(std::int64_t delta_ns) noexcept
        {
            ::timex tx {};
            tx.modes  = ADJ_SETOFFSET | ADJ_NANO;
            tx.time.tv_sec  = delta_ns / 1'000'000'000LL;
            tx.time.tv_usec = delta_ns % 1'000'000'000LL;  // tv_usec holds ns with ADJ_NANO
            ::clock_adjtime(CLOCK_TAI, &tx);
        }

        // PI gains — tuned for ~125 Hz Sync rate (8 ms period)
        static constexpr double kp_ { 0.7 };   ///< Proportional gain
        static constexpr double ki_ { 0.3 };   ///< Integral gain

        bool        initialized_ { false };
        bool        synced_      { false };
        double      integral_    { 0.0 };
        std::int64_t last_offset_ { 0 };
    };

} // namespace kmx::aio::avb::gptp
