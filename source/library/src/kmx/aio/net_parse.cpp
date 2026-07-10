#include <kmx/aio/ipv4.hpp>
#include <kmx/aio/ipv6.hpp>
#include <kmx/aio/mac.hpp>

#include <array>
#include <charconv>

namespace kmx::aio
{
    namespace detail
    {
        // Shared hex conversion table and utility for all network address parsers
        constexpr std::array<std::int8_t, 256u> make_hex_table() noexcept
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

        inline constexpr std::array<std::int8_t, 256u> hex_table = make_hex_table();

        [[nodiscard]] constexpr int hex_to_val(const char c) noexcept
        {
            return hex_table[static_cast<std::uint8_t>(c)];
        }
    } // namespace detail

    bool parse_ipv4_address(std::string_view text, ipv4_storage_t& out) noexcept
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

            if (ec != std::errc {} || ptr == start || val > 255u)
                return false;

            buf[i] = static_cast<std::uint8_t>(val);
            p = ptr;

            if (i < 3u)
            {
                if (p >= end || *p != '.')
                    return false;
                ++p;
            }
        }

        if (p != end)
            return false;

        out = buf;
        return true;
    }

    bool parse_ipv6_address(std::string_view text, ipv6_storage_t& out) noexcept
    {
        // Simplified IPv6 parser: only supports fully expanded format (8 groups of 4 hex digits)
        // Format: xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx
        // e.g.: 2001:0db8:0000:0000:0000:0000:0000:0001
        std::array<std::uint8_t, 16u> buf {};
        const char* p = text.data();
        const char* const end = p + text.size();

        for (std::size_t i = 0u; i < 8u; ++i)
        {
            if (p >= end)
                return false;

            // Parse 4 hex digits (one 16-bit group)
            int v0 = detail::hex_to_val(*p);
            if (v0 < 0)
                return false;
            ++p;

            if (p >= end)
                return false;
            int v1 = detail::hex_to_val(*p);
            if (v1 < 0)
                return false;
            ++p;

            if (p >= end)
                return false;
            int v2 = detail::hex_to_val(*p);
            if (v2 < 0)
                return false;
            ++p;

            if (p >= end)
                return false;
            int v3 = detail::hex_to_val(*p);
            if (v3 < 0)
                return false;
            ++p;

            std::uint16_t group = ((v0 << 12) | (v1 << 8) | (v2 << 4) | v3);
            buf[i * 2u] = static_cast<std::uint8_t>(group >> 8);
            buf[i * 2u + 1u] = static_cast<std::uint8_t>(group & 0xFF);

            if (i < 7u)
            {
                if (p >= end || *p != ':')
                    return false;
                ++p;
            }
        }

        if (p != end)
            return false;

        out = buf;
        return true;
    }

    bool parse_mac_address(std::string_view text, mac_storage_t& out) noexcept
    {
        const char* p = text.data();
        const char* const end = p + text.size();
        std::array<std::uint8_t, 6u> buf {};

        for (std::size_t i = 0u; i < 6u; ++i)
        {
            if (p >= end)
                return false;

            const int v0 = detail::hex_to_val(*p);
            if (v0 < 0)
                return false;
            ++p;

            int val = v0;
            if ((p < end) && (*p != ':'))
            {
                const int v1 = detail::hex_to_val(*p);
                if (v1 < 0)
                    return false;
                val = (val << 4) | v1;
                ++p;
            }

            buf[i] = static_cast<std::uint8_t>(val);
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

} // namespace kmx::aio
