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
        void update(std::int64_t offset_ns, std::int64_t path_delay_ns) noexcept;

        /// @brief Returns true if the servo considers the clock synchronized.
        [[nodiscard]] bool is_synced() const noexcept { return synced_; }

        /// @brief Current filtered offset in nanoseconds.
        [[nodiscard]] std::int64_t last_offset() const noexcept { return last_offset_; }

        /// @brief Reset servo state (e.g., after master change).
        void reset() noexcept;

    private:
        void step_clock(std::int64_t delta_ns) noexcept;

        // PI gains — tuned for ~125 Hz Sync rate (8 ms period)
        static constexpr double kp_ {0.7}; ///< Proportional gain
        static constexpr double ki_ {0.3}; ///< Integral gain

        bool initialized_ {};
        bool synced_ {};
        double integral_ {};
        std::int64_t last_offset_ {};
    };

} // namespace kmx::aio::avb::gptp
