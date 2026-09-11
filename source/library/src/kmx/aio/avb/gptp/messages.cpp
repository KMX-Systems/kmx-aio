/// @file src/kmx/aio/avb/gptp/messages.cpp
/// @brief The compiled body of the gPTP clock-identity helper.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/avb/gptp/messages.hpp>

#include <kmx/aio/avb/gptp/header.hpp>

namespace kmx::aio::avb::gptp
{
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
