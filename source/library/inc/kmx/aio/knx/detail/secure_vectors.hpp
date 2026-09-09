/// @file aio/knx/detail/secure_vectors.hpp
/// @brief Golden wire vectors for this build's placeholder secure envelope.
/// @details
/// One vector per profile, each the octets @ref kmx::aio::knx::secure::encode_secure_packet produces for a
/// known sequence number and payload. They exist so a change to the envelope's layout fails a test rather
/// than silently altering what this build puts on the wire.
///
/// The payloads are deliberately opaque: this envelope's provider is an injection point, so a vector that
/// pinned protected octets would only be pinning whatever test double produced them.
/// @warning Not KNX Secure. These pin the layout of kmx::aio::knx::secure, whose service type is
///          deliberately unassigned; see the warning on kmx::aio::knx::secure::secure_service.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstdint>
#endif

#include <kmx/aio/knx/secure.hpp>

namespace kmx::aio::knx::detail::secure_vectors
{
    /// @brief The sequence number the IP Secure vector is framed under.
    inline constexpr std::uint64_t ip_secure_sequence = 0x0102030405060708ull;
    /// @brief The payload the IP Secure vector carries.
    inline constexpr std::array<std::uint8_t, 3u> ip_secure_payload {0xAAu, 0xBBu, 0xCCu};
    /// @brief The octets an IP Secure envelope encodes to: header, profile, sequence, length, payload.
    inline constexpr std::array<std::uint8_t, 21u> ip_secure_wire {
        0x06u, 0x10u, 0xFFu, 0x00u, 0x00u, 0x15u,
        0x01u, 0x00u,
        0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u,
        0x00u, 0x03u,
        0xAAu, 0xBBu, 0xCCu,
    };

    /// @brief The sequence number the Data Secure vector is framed under.
    inline constexpr std::uint64_t data_secure_sequence = 0x1112131415161718ull;
    /// @brief The payload the Data Secure vector carries.
    inline constexpr std::array<std::uint8_t, 4u> data_secure_payload {0x01u, 0x02u, 0x03u, 0x04u};
    /// @brief The octets a Data Secure envelope encodes to; identical in shape, differing in profile octet.
    inline constexpr std::array<std::uint8_t, 22u> data_secure_wire {
        0x06u, 0x10u, 0xFFu, 0x00u, 0x00u, 0x16u,
        0x02u, 0x00u,
        0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u, 0x18u,
        0x00u, 0x04u,
        0x01u, 0x02u, 0x03u, 0x04u,
    };
}
