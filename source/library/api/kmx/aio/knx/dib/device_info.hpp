/// @file api/kmx/aio/knx/dib/device_info.hpp
/// @brief The DEVICE_INFO description information block: what a KNXnet/IP device is.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @reference KNX System Specifications, 03/08/02 "Core", description information block.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/ipv4.hpp>
        #include <kmx/aio/knx/individual_address.hpp>
        #include <kmx/aio/mac.hpp>

        #include <array>
        #include <cstdint>
        #include <string_view>
    #endif

namespace kmx::aio::knx::dib
{
    /// @brief Twisted pair 0, a bit of the communication media mask of a DEVICE_INFO block.
    inline constexpr std::uint8_t medium_tp0 = 0x01u;
    /// @brief Twisted pair 1, the medium of most KNX installations.
    inline constexpr std::uint8_t medium_tp1 = 0x02u;
    /// @brief Powerline 110.
    inline constexpr std::uint8_t medium_pl110 = 0x04u;
    /// @brief Powerline 132.
    inline constexpr std::uint8_t medium_pl132 = 0x08u;
    /// @brief KNX radio frequency.
    inline constexpr std::uint8_t medium_rf = 0x10u;
    /// @brief KNXnet/IP.
    inline constexpr std::uint8_t medium_ip = 0x20u;

    /// @brief The six-octet KNX serial number of a device.
    /// @details Its own alias rather than a bare array: a serial number and a MAC address are both six
    ///          octets, and a function taking either would accept the other silently.
    using serial_number_t = std::array<std::uint8_t, 6u>;

    /// @brief Length of the friendly name field, which is fixed and NUL padded.
    inline constexpr std::size_t friendly_name_size = 30u;

    /// @brief A DEVICE_INFO block.
    struct device_info
    {
        /// @brief The media the device supports, as a mask of the `medium_*` values.
        std::uint8_t knx_medium = medium_ip;
        /// @brief The device status; bit zero is the programming mode flag.
        std::uint8_t device_status {};
        /// @brief The device's own individual address.
        individual_address address {};
        /// @brief The project installation identifier.
        std::uint16_t project_installation_id {};
        /// @brief The six-octet KNX serial number.
        serial_number_t serial_number {};
        /// @brief The routing multicast address, all-zero when the device does not route.
        ipv4::storage_t multicast_address {};
        /// @brief The device's MAC address.
        mac::storage_t mac_address {};
        /// @brief The friendly name, NUL padded to @ref friendly_name_size octets.
        std::array<char, friendly_name_size> friendly_name {};

        /// @brief Indicates whether the device is in programming mode.
        [[nodiscard]] constexpr bool programming_mode() const noexcept { return (device_status & 0x01u) != 0u; }

        /// @brief Returns the friendly name without its NUL padding.
        [[nodiscard]] constexpr std::string_view name() const noexcept
        {
            std::size_t length {};
            while ((length < friendly_name.size()) && (friendly_name[length] != '\0'))
                ++length;
            return {friendly_name.data(), length};
        }

        /// @brief Sets the friendly name, truncating anything past @ref friendly_name_size octets.
        constexpr void set_name(const std::string_view value) noexcept
        {
            friendly_name = {};
            const auto length = (value.size() < friendly_name.size()) ? value.size() : friendly_name.size();
            for (std::size_t i {}; i < length; ++i)
                friendly_name[i] = value[i];
        }
    };
}
#endif // KMX_AIO_FEATURE_KNX
