/// @file inc/kmx/aio/detail/hex.hpp
/// @brief Hexadecimal digit decoding shared by the text parsers of network addresses.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstddef>
    #include <cstdint>
#endif

namespace kmx::aio::detail
{
    /// @brief Builds the table mapping every character code to its value as a hexadecimal digit.
    /// @return A table holding the digit value at the code of each of `0-9`, `a-f` and `A-F`, and -1 everywhere else.
    [[nodiscard]] constexpr std::array<std::int8_t, 256u> make_hex_table() noexcept
    {
        std::array<std::int8_t, 256u> table {};
        for (auto& entry: table)
            entry = -1;
        for (int c = '0'; c <= '9'; ++c)
            table[static_cast<std::size_t>(c)] = static_cast<std::int8_t>(c - '0');
        for (int c = 'a'; c <= 'f'; ++c)
            table[static_cast<std::size_t>(c)] = static_cast<std::int8_t>(c - 'a' + 10);
        for (int c = 'A'; c <= 'F'; ++c)
            table[static_cast<std::size_t>(c)] = static_cast<std::int8_t>(c - 'A' + 10);
        return table;
    }

    /// @brief The value of every character code as a hexadecimal digit, or -1 where it is not one.
    inline constexpr std::array<std::int8_t, 256u> hex_table = make_hex_table();

    /// @brief Decodes one hexadecimal digit.
    /// @param c The character to decode.
    /// @return The digit's value, 0 to 15, or -1 when `c` is not a hexadecimal digit.
    [[nodiscard]] constexpr int hex_to_val(const char c) noexcept
    {
        return hex_table[static_cast<std::uint8_t>(c)];
    }
}
