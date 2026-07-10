/// @file aio/ipv4.hpp
/// @brief IPv4 address storage and view types.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstdint>
    #include <span>
#endif

namespace kmx::aio
{
    /// @brief Owned IPv4 storage container.
    using ipv4_storage_t = std::array<std::uint8_t, 4u>;
    /// @brief Owned IPv4 address alias.
    using ipv4_address_owned_t = ipv4_storage_t;
    /// @brief Non-owning IPv4 address view.
    using ipv4_address_t = std::span<const std::uint8_t, 4u>;

    /// @brief Loopback IPv4 address in network byte order.
    inline constexpr ipv4_storage_t localhost_ipv4 {127u, 0u, 0u, 1u};
    /// @brief Wildcard IPv4 address in network byte order.
    inline constexpr ipv4_storage_t any_ipv4 {0u, 0u, 0u, 0u};

    /// @brief Creates a non-owning IPv4 address view.
    /// @param ip The owned IPv4 bytes.
    /// @return A view over the IPv4 storage.
    [[nodiscard]] constexpr ipv4_address_t make_ipv4_address(const ipv4_storage_t& ip) noexcept
    {
        return ipv4_address_t {ip};
    }

} // namespace kmx::aio
