#include <kmx/aio/sample/common/cli_parse.hpp>

#include <array>
#include <charconv>
#include <cstdint>
#include <limits>

namespace kmx::aio::sample::common
{
    namespace detail
    {
        template <typename T>
        bool parse_unsigned_sv(std::string_view text, T& out)
        {
            const char* begin = text.data();
            const char* end = text.data() + text.size();
            T value {};
            const auto [ptr, ec] = std::from_chars(begin, end, value);
            if ((ec != std::errc {}) || (ptr != end))
                return false;

            out = value;
            return true;
        }

        template <typename T>
        bool parse_unsigned_cstr_impl(const char* raw, T& out)
        {
            if (!raw)
                return false;

            std::uint64_t parsed {};
            const char* begin = raw;
            const char* end = raw + std::char_traits<char>::length(raw);
            const auto [ptr, ec] = std::from_chars(begin, end, parsed);
            if ((ec != std::errc {}) || (ptr != end))
                return false;

            if (parsed > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
                return false;

            out = static_cast<T>(parsed);
            return true;
        }

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
    }

    bool parse_unsigned_u16(std::string_view text, std::uint16_t& out)
    {
        return detail::parse_unsigned_sv(text, out);
    }

    bool parse_unsigned_u64(std::string_view text, std::uint64_t& out)
    {
        return detail::parse_unsigned_sv(text, out);
    }

    bool parse_unsigned_u16_cstr(const char* raw, std::uint16_t& out)
    {
        return detail::parse_unsigned_cstr_impl(raw, out);
    }

    bool parse_unsigned_u32_cstr(const char* raw, std::uint32_t& out)
    {
        return detail::parse_unsigned_cstr_impl(raw, out);
    }

    bool parse_unsigned_u64_cstr(const char* raw, std::uint64_t& out)
    {
        return detail::parse_unsigned_cstr_impl(raw, out);
    }

    bool parse_mac_bytes(std::string_view text, std::array<std::uint8_t, 6u>& out) noexcept
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
}
