/// @file aio/ipv6.hpp
/// @brief IPv6 address storage and view types.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstdint>
    #include <span>
#endif

namespace kmx::aio
{
    /// @brief Owned IPv6 storage container.
    using ipv6_storage_t = std::array<std::uint8_t, 16u>;
    /// @brief Owned IPv6 address alias.
    using ipv6_address_owned_t = ipv6_storage_t;
    /// @brief Non-owning IPv6 address view.
    using ipv6_address_t = std::span<const std::uint8_t, 16u>;

    /// @brief Creates a non-owning IPv6 address view.
    /// @param ip The owned IPv6 bytes.
    /// @return A view over the IPv6 storage.
    [[nodiscard]] constexpr ipv6_address_t make_ipv6_address(const ipv6_storage_t& ip) noexcept
    {
        return ipv6_address_t {ip};
    }

} // namespace kmx::aio
