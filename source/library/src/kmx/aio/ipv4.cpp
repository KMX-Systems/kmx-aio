/// @file src/kmx/aio/ipv4.cpp
/// @brief Text parser for IPv4 addresses in dotted-decimal notation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/ipv4.hpp>
#ifndef PCH
    #include <array>
    #include <charconv>
#endif

namespace kmx::aio::ipv4
{
    bool parse_address(std::string_view text, storage_t& out) noexcept
    {
        std::array<std::uint8_t, 4u> buf {};
        const char* p = text.data();
        const char* const end = p + text.size();

        for (std::size_t i = 0u; i < 4u; ++i)
        {
            if (p >= end)
                return false;

            std::uint32_t val {};
            const char* const start = p;
            const auto [ptr, ec] = std::from_chars(p, end, val);

            if ((ec != std::errc {}) || (ptr == start) || (val > 255u))
                return false;

            buf[i] = static_cast<std::uint8_t>(val);
            p = ptr;

            if (i < 3u)
            {
                if ((p >= end) || (*p != '.'))
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
