/// @file inc/kmx/aio/avb/gptp/timestamp.hpp
/// @brief The IEEE 802.1AS gPTP wire timestamp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/avb/avb_types.hpp>

    #include <array>
    #include <cstdint>
#endif

namespace kmx::aio::avb::gptp
{
    /// @brief gPTP wire timestamp: a 48-bit seconds field followed by a 32-bit nanoseconds field.
    struct timestamp
    {
        std::array<std::uint8_t, 6u> seconds_msb {}; ///< seconds[47:16]
        std::uint32_t nanoseconds {};                ///< in network byte order

        /// @brief Convert to nanoseconds since epoch (host byte order).
        /// @return The timestamp expressed as nanoseconds since the PTP epoch.
        [[nodiscard]] tai_timestamp_t to_ns() const noexcept;

        /// @brief Builds a wire timestamp from nanoseconds since epoch.
        /// @param ns Nanoseconds since the PTP epoch, in host byte order.
        /// @return The equivalent wire-encoded timestamp.
        static timestamp from_ns(tai_timestamp_t ns) noexcept;
    };
}
