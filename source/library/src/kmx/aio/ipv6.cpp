/// @file src/kmx/aio/ipv6.cpp
/// @brief Text parser for IPv6 addresses in fully expanded notation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/ipv6.hpp>
#ifndef PCH
    #include <kmx/aio/detail/hex.hpp>

    #include <array>
    #include <cstddef>
#endif

namespace kmx::aio::ipv6
{
    /// @brief Reads one fully expanded IPv6 group: exactly four hexadecimal digits.
    /// @param p The cursor, advanced past the digits when they are read.
    /// @param end One past the last character available.
    /// @param group Receives the group's value.
    /// @return `true` when four digits were read.
    [[nodiscard]] static bool read_hex_group(const char*& p, const char* const end, std::uint16_t& group) noexcept
    {
        std::uint16_t value {};
        for (std::size_t digit = 0u; digit < 4u; ++digit)
        {
            if (p >= end)
                return false;
            const auto nibble = detail::hex_to_val(*p);
            if (nibble < 0)
                return false;

            value = static_cast<std::uint16_t>((value << 4u) | static_cast<std::uint16_t>(nibble));
            ++p;
        }

        group = value;
        return true;
    }

    bool parse_address(std::string_view text, storage_t& out) noexcept
    {
        // Only the fully expanded form is accepted - eight groups of four hexadecimal digits, as in
        // 2001:0db8:0000:0000:0000:0000:0000:0001. The "::" abbreviation is deliberately not read here.
        std::array<std::uint8_t, 16u> buf {};
        const char* p = text.data();
        const char* const end = p + text.size();

        for (std::size_t i = 0u; i < 8u; ++i)
        {
            std::uint16_t group {};
            if (!read_hex_group(p, end, group))
                return false;

            buf[i * 2u] = static_cast<std::uint8_t>(group >> 8u);
            buf[i * 2u + 1u] = static_cast<std::uint8_t>(group & 0xFFu);

            // Seven separators for eight groups: the last group is followed by the end of the text.
            if (i < 7u)
            {
                if ((p >= end) || (*p != ':'))
                    return false;
                ++p;
            }
        }

        if (p != end)
            return false;

        out = buf;
        return true;
    }
}
