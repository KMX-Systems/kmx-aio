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
        bool parse_unsigned_sv(const std::string_view text, T& out) noexcept
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
        bool parse_unsigned_cstr_impl(const char* raw, T& out) noexcept
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
    } // namespace detail

    bool parse_unsigned_u16(const std::string_view text, std::uint16_t& out) noexcept
    {
        return detail::parse_unsigned_sv(text, out);
    }

    bool parse_unsigned_u64(const std::string_view text, std::uint64_t& out) noexcept
    {
        return detail::parse_unsigned_sv(text, out);
    }

    bool parse_unsigned_u16_cstr(const char* raw, std::uint16_t& out) noexcept
    {
        return detail::parse_unsigned_cstr_impl(raw, out);
    }

    bool parse_unsigned_u32_cstr(const char* raw, std::uint32_t& out) noexcept
    {
        return detail::parse_unsigned_cstr_impl(raw, out);
    }

    bool parse_unsigned_u64_cstr(const char* raw, std::uint64_t& out) noexcept
    {
        return detail::parse_unsigned_cstr_impl(raw, out);
    }
}
