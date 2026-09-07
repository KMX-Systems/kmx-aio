/// @file aio/knx/detail/secure_vectors.hpp
/// @brief Golden wire vectors for KNX Secure packet envelopes.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstdint>
#endif

#include <kmx/aio/knx/secure.hpp>

namespace kmx::aio::knx::detail::secure_vectors
{
    inline constexpr std::uint64_t ip_secure_sequence = 0x0102030405060708ull;
    inline constexpr std::array<std::uint8_t, 3u> ip_secure_payload {0xAAu, 0xBBu, 0xCCu};
    inline constexpr std::array<std::uint8_t, 21u> ip_secure_wire {
        0x06u, 0x10u, 0x09u, 0x50u, 0x00u, 0x15u,
        0x01u, 0x00u,
        0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u,
        0x00u, 0x03u,
        0xAAu, 0xBBu, 0xCCu,
    };

    inline constexpr std::uint64_t data_secure_sequence = 0x1112131415161718ull;
    inline constexpr std::array<std::uint8_t, 4u> data_secure_payload {0x01u, 0x02u, 0x03u, 0x04u};
    inline constexpr std::array<std::uint8_t, 22u> data_secure_wire {
        0x06u, 0x10u, 0x09u, 0x50u, 0x00u, 0x16u,
        0x02u, 0x00u,
        0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u, 0x18u,
        0x00u, 0x04u,
        0x01u, 0x02u, 0x03u, 0x04u,
    };
}
