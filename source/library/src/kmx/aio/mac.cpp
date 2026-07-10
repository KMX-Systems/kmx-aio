#include <kmx/aio/mac.hpp>

#include <array>

namespace kmx::aio
{
    namespace detail
    {
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
