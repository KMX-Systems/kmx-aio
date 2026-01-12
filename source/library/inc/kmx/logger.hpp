#pragma once
#ifndef PCH
    #include <array>
    #include <cstring>
    #include <format>
    #include <print>
    #include <source_location>
    #include <string_view>
#endif

namespace kmx::logger
{
    enum class level
    {
        debug,
        info,
        warn,
        error
    };

    namespace detail
    {
        /// @brief Internal helper to format log levels as a single char using std::array.
        constexpr char level_to_char(const level l) noexcept
        {
            static constexpr std::array<char, static_cast<std::size_t>(level::error) + 2u> chars {
                'D', // debug
                'I', // info
                'W', // warn
                'E', // error
                '?'  // unknown
            };
            const auto index = static_cast<std::size_t>(l);
            return index < chars.size() ? chars[index] : chars.back();
        }
    }

    /// @brief Logs a formatted message to stdout/stderr.
    /// @note Guaranteed not to throw; exceptions are swallowed to prevent crash during logging.
    template <typename... Args>
    void log(const level lvl, const std::source_location& loc, std::format_string<Args...> fmt, Args&&... args) noexcept
    {
        try
        {
            // Extract only the file name from the full path
            auto full = loc.file_name();
            const char* file = full;
            if (const char* last_slash = std::strrchr(full, '/'))
                file = last_slash + 1;
            // Route error messages to stderr (unbuffered), others to stdout
            // Manually flush to ensure immediate output
            if (lvl == level::error)
            {
                std::println(stderr, "[{0}] [{1}:{2}] {3}", detail::level_to_char(lvl), file, loc.line(),
                             std::format(fmt, std::forward<Args>(args)...));
                std::fflush(stderr);
            }
            else
            {
                std::println(stdout, "[{0}] [{1}:{2}] {3}", detail::level_to_char(lvl), file, loc.line(),
                             std::format(fmt, std::forward<Args>(args)...));
                std::fflush(stdout);
            }
        }
        catch (...)
        {
            // Swallow exceptions
        }
    }

} // namespace logger
