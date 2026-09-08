/// @file kmx/aio/avb/gptp/messages.cpp
/// @brief The compiled body of the gPTP timestamp and clock-identity helpers.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/avb/gptp/messages.hpp>

namespace kmx::aio::avb::gptp
{
    avb_timestamp_t timestamp_t::to_ns() const noexcept
    {
        std::uint64_t sec {};
        for (int i = 0; i < 6; ++i)
            sec = (sec << 8u) | seconds_msb[static_cast<std::size_t>(i)];
        return sec * 1'000'000'000ULL + ::ntohl(nanoseconds);
    }

    timestamp_t timestamp_t::from_ns(avb_timestamp_t ns) noexcept
    {
        const std::uint64_t sec = ns / 1'000'000'000ULL;
        const std::uint32_t nsec = static_cast<std::uint32_t>(ns % 1'000'000'000ULL);
        timestamp_t ts {};
        for (int i = 5; i >= 0; --i)
            ts.seconds_msb[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(sec >> (8 * (5 - i)));

        ts.nanoseconds = ::htonl(nsec);
        return ts;
    }

    clock_identity_t mac_to_clock_id(const mac_address_t& mac) noexcept
    {
        clock_identity_t id {};
        // Insert 0xFF 0xFE in the middle per IEEE EUI-64
        id.id[0u] = mac[0u] ^ 0x02u; // flip U/L bit
        id.id[1u] = mac[1u];
        id.id[2u] = mac[2u];
        id.id[3u] = 0xFFu;
        id.id[4u] = 0xFEu;
        id.id[5u] = mac[3u];
        id.id[6u] = mac[4u];
        id.id[7u] = mac[5u];
        return id;
    }
}
