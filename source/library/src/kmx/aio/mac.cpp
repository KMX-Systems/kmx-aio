/// @file src/kmx/aio/mac.cpp
/// @brief Text parser for MAC addresses in colon-separated hexadecimal notation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/mac.hpp>
#ifndef PCH
    #include <kmx/aio/detail/hex.hpp>

    #include <array>
    #include <cstddef>
#endif

namespace kmx::aio::mac
{
    /// @brief Reads one MAC octet: one or two hexadecimal digits.
    /// @param p The cursor, advanced past the digits when they are read.
    /// @param end One past the last character available.
    /// @param octet Receives the octet's value.
    /// @return `true` when at least one digit was read.
    /// @details A second digit is taken only when one is there: "1:2:3:4:5:6" and "01:02:03:04:05:06"
    ///          name the same address, and both spellings are in use.
    [[nodiscard]] static bool read_octet(const char*& p, const char* const end, std::uint8_t& octet) noexcept
    {
        if (p >= end)
            return false;
        const auto high = detail::hex_to_val(*p);
        if (high < 0)
            return false;
        ++p;

        auto value = high;
        if ((p < end) && (*p != ':'))
        {
            const auto low = detail::hex_to_val(*p);
            if (low < 0)
                return false;
            value = (value << 4) | low;
            ++p;
        }

        octet = static_cast<std::uint8_t>(value);
        return true;
    }

    bool parse_address(std::string_view text, storage_t& out) noexcept
    {
        const char* p = text.data();
        const char* const end = p + text.size();
        std::array<std::uint8_t, 6u> buf {};

        for (std::size_t i = 0u; i < 6u; ++i)
        {
            if (!read_octet(p, end, buf[i]))
                return false;

            // Five separators for six octets: the last octet is followed by the end of the text.
            if (i < 5u)
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
