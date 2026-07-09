#include <kmx/aio/sample/common/cli_parse.hpp>

#include <charconv>
#include <limits>
#include <sstream>
#include <string>

namespace kmx::aio::sample::common
{
    namespace
    {
        template <typename T>
        bool parse_unsigned_sv(std::string_view text, T& out)
        {
            const char* begin = text.data();
            const char* end = text.data() + text.size();
            T value {};
            const auto [ptr, ec] = std::from_chars(begin, end, value);
            if (ec != std::errc {} || ptr != end)
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
            if (ec != std::errc {} || ptr != end)
                return false;

            if (parsed > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
                return false;

            out = static_cast<T>(parsed);
            return true;
        }
    }

    bool parse_unsigned_u16(std::string_view text, std::uint16_t& out)
    {
        return parse_unsigned_sv(text, out);
    }

    bool parse_unsigned_u64(std::string_view text, std::uint64_t& out)
    {
        return parse_unsigned_sv(text, out);
    }

    bool parse_unsigned_u16_cstr(const char* raw, std::uint16_t& out)
    {
        return parse_unsigned_cstr_impl(raw, out);
    }

    bool parse_unsigned_u32_cstr(const char* raw, std::uint32_t& out)
    {
        return parse_unsigned_cstr_impl(raw, out);
    }

    bool parse_unsigned_u64_cstr(const char* raw, std::uint64_t& out)
    {
        return parse_unsigned_cstr_impl(raw, out);
    }

    bool parse_mac_bytes(std::string_view text, std::array<std::uint8_t, 6u>& out)
    {
        unsigned int b0 {}, b1 {}, b2 {}, b3 {}, b4 {}, b5 {};
        std::stringstream ss(std::string {text});
        ss >> std::hex >> b0;
        if (ss.fail() || ss.get() != ':')
            return false;
        ss >> std::hex >> b1;
        if (ss.fail() || ss.get() != ':')
            return false;
        ss >> std::hex >> b2;
        if (ss.fail() || ss.get() != ':')
            return false;
        ss >> std::hex >> b3;
        if (ss.fail() || ss.get() != ':')
            return false;
        ss >> std::hex >> b4;
        if (ss.fail() || ss.get() != ':')
            return false;
        ss >> std::hex >> b5;
        if (ss.fail() || !ss.eof())
            return false;

        if (b0 > 0xFFu || b1 > 0xFFu || b2 > 0xFFu || b3 > 0xFFu || b4 > 0xFFu || b5 > 0xFFu)
            return false;

        out = {
            static_cast<std::uint8_t>(b0), static_cast<std::uint8_t>(b1), static_cast<std::uint8_t>(b2),
            static_cast<std::uint8_t>(b3), static_cast<std::uint8_t>(b4), static_cast<std::uint8_t>(b5),
        };
        return true;
    }
}
