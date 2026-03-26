/// @file avb/avb_types.hpp
/// @brief AVB-specific types shared across the AVB pillar.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <array>
#include <cstdint>

namespace kmx::aio::avb
{
    /// @brief MAC address (6 bytes).
    using mac_address_t = std::array<std::uint8_t, 6u>;

    /// @brief AVB Stream ID = source MAC + 2-byte unique identifier (IEEE 1722).
    struct stream_id_t
    {
        mac_address_t source_mac {};
        std::uint16_t unique_id {};

        [[nodiscard]] constexpr bool operator==(const stream_id_t&) const noexcept = default;
    };

    /// @brief IEEE 802.1Q VLAN tag. AVB streams typically use VLAN 2 with PCP 3 (Class A).
    struct vlan_tag_t
    {
        std::uint16_t tpid { 0x8100u };  ///< Tag Protocol ID (always 0x8100)
        std::uint8_t  pcp  { 3u };       ///< Priority Code Point: 3=Class A, 2=Class B
        std::uint8_t  dei  { 0u };       ///< Drop Eligible Indicator
        std::uint16_t vid  { 2u };       ///< VLAN ID (AVB default: 2)
    };

    /// @brief Hardware-precise timestamp in nanoseconds since the TAI epoch.
    /// @note Use CLOCK_TAI (not CLOCK_REALTIME) for AVB/PTP operations.
    using avb_timestamp_t = std::uint64_t;

    /// @brief EtherType constants used in AVB.
    namespace ethertype
    {
        inline constexpr std::uint16_t avtp    = 0x22F0;  ///< IEEE 1722 AVTP
        inline constexpr std::uint16_t gptp    = 0x88F7;  ///< IEEE 802.1AS gPTP
        inline constexpr std::uint16_t msrp    = 0x22EA;  ///< IEEE 802.1Qat SRP
        inline constexpr std::uint16_t vlan    = 0x8100;  ///< IEEE 802.1Q VLAN
    }

    /// @brief Well-known AVB multicast MAC addresses.
    namespace multicast
    {
        /// @brief gPTP peer-delay multicast (01:80:C2:00:00:0E)
        inline constexpr mac_address_t gptp_peer { 0x01u, 0x80u, 0xC2u, 0x00u, 0x00u, 0x0Eu };
        /// @brief SRP (MSRP/MMRP) multicast (01:80:C2:00:00:21)
        inline constexpr mac_address_t srp       { 0x01u, 0x80u, 0xC2u, 0x00u, 0x00u, 0x21u };
    }
}
