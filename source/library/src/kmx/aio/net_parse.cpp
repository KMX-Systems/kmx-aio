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

} // namespace kmx::aio

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

} // namespace kmx::aio::ipv4

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

} // namespace kmx::aio::ipv6

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

} // namespace kmx::aio::mac
