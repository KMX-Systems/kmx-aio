/// @file aio/ipv4.hpp
/// @brief IPv4 address storage and view types.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstdint>
    #include <span>
    #include <string_view>
#endif

namespace kmx::aio::ipv4
{
    /// @brief Owned IPv4 storage container.
    using storage_t = std::array<std::uint8_t, 4u>;
    /// @brief Owned IPv4 address alias.
    using address_owned_t = storage_t;
    /// @brief Non-owning IPv4 address view.
    using address_t = std::span<const std::uint8_t, 4u>;

    /// @brief Loopback IPv4 address in network byte order.
    inline constexpr storage_t localhost {127u, 0u, 0u, 1u};
    /// @brief Wildcard IPv4 address in network byte order.
    inline constexpr storage_t any {0u, 0u, 0u, 0u};

    /// @brief Creates a non-owning IPv4 address view.
    /// @param ip The owned IPv4 bytes.
    /// @return A view over the IPv4 storage.
    [[nodiscard]] constexpr address_t make_address(const storage_t& ip) noexcept
    {
        return address_t {ip};
    }

    /// @brief Parse an IPv4 address from dotted-decimal format (e.g., "192.168.1.1").
    /// @param text Input string in dotted-decimal format.
    /// @param out Output IPv4 address storage on success.
    /// @return true if parsing succeeded, false otherwise.
    [[nodiscard]] bool parse_address(std::string_view text, storage_t& out) noexcept;

} // namespace kmx::aio::ipv4
