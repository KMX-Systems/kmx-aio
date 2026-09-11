/// @file src/kmx/aio/avb/gptp/timestamp.cpp
/// @brief The compiled body of the gPTP wire timestamp conversions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/avb/gptp/timestamp.hpp>
#ifndef PCH
    #include <cstddef>
    #include <arpa/inet.h>
#endif

namespace kmx::aio::avb::gptp
{
    tai_timestamp_t timestamp::to_ns() const noexcept
    {
        std::uint64_t sec {};
        for (int i = 0; i < 6; ++i)
            sec = (sec << 8u) | seconds_msb[static_cast<std::size_t>(i)];
        return sec * 1'000'000'000ULL + ::ntohl(nanoseconds);
    }

    timestamp timestamp::from_ns(tai_timestamp_t ns) noexcept
    {
        const std::uint64_t sec = ns / 1'000'000'000ULL;
        const std::uint32_t nsec = static_cast<std::uint32_t>(ns % 1'000'000'000ULL);
        timestamp ts {};
        for (int i = 5; i >= 0; --i)
            ts.seconds_msb[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(sec >> (8 * (5 - i)));

        ts.nanoseconds = ::htonl(nsec);
        return ts;
    }
}
