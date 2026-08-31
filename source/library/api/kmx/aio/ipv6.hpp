/// @file aio/ipv6.hpp
/// @brief IPv6 address storage and view types.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstdint>
    #include <span>
    #include <string_view>
#endif

namespace kmx::aio::ipv6
{
    /// @brief Owned IPv6 storage container.
    using storage_t = std::array<std::uint8_t, 16u>;
    /// @brief Owned IPv6 address alias.
    using address_owned_t = storage_t;
    /// @brief Non-owning IPv6 address view.
    using address_t = std::span<const std::uint8_t, 16u>;

    /// @brief Creates a non-owning IPv6 address view.
    /// @param ip The owned IPv6 bytes.
    /// @return A view over the IPv6 storage.
    [[nodiscard]] constexpr address_t make_address(const storage_t& ip) noexcept
    {
        return address_t {ip};
    }

    /// @brief Parse an IPv6 address from standard notation (e.g., "2001:db8::1" or "::1").
    /// @param text Input string in IPv6 notation.
    /// @param out Output IPv6 address storage on success.
    /// @return true if parsing succeeded, false otherwise.
    [[nodiscard]] bool parse_address(std::string_view text, storage_t& out) noexcept;

} // namespace kmx::aio::ipv6
