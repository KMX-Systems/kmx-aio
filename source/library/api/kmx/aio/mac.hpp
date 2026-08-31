/// @file aio/mac.hpp
/// @brief MAC address storage and view types.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstdint>
    #include <span>
    #include <string_view>
#endif

namespace kmx::aio::mac
{
    /// @brief Owned MAC address storage container.
    using storage_t = std::array<std::uint8_t, 6u>;
    /// @brief Owned MAC address alias.
    using address_owned_t = storage_t;
    /// @brief Non-owning MAC address view.
    using address_t = std::span<const std::uint8_t, 6u>;

    /// @brief Broadcast MAC address (FF:FF:FF:FF:FF:FF).
    inline constexpr storage_t broadcast {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};

    /// @brief Creates a non-owning MAC address view.
    /// @param address The owned MAC address bytes.
    /// @return A view over the MAC address storage.
    [[nodiscard]] constexpr address_t make_address(const storage_t& address) noexcept
    {
        return address_t {address};
    }

    /// @brief Parse a MAC address from string format (e.g., "AA:BB:CC:DD:EE:FF").
    /// @param text Input string in colon-separated hex format.
    /// @param out Output MAC address storage on success.
    /// @return true if parsing succeeded, false otherwise.
    [[nodiscard]] bool parse_address(std::string_view text, storage_t& out) noexcept;

} // namespace kmx::aio::mac
