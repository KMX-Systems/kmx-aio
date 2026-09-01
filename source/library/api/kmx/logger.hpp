#pragma once
#ifndef PCH
    #include <array>
    #include <cstring>
    #include <format>
    #include <print>
    #include <source_location>
#endif

namespace kmx::logger
{
    /// @brief Severity of a log record, in increasing order.
    enum class level
    {
        /// @brief Diagnostic detail useful only while debugging.
        debug,
        /// @brief Ordinary progress information.
        info,
        /// @brief A condition worth attention that did not stop the operation.
        warn,
        /// @brief An operation failed.
        error
    };

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
            // LCOV_EXCL_BR_LINE: __FILE__ as this project compiles it always carries a directory, so
            // the no-separator arm is never taken; it is here for a build that does not.
            if (const char* last_slash = std::strrchr(full, '/')) // LCOV_EXCL_BR_LINE
                file = last_slash + 1;
            // Route error messages to stderr (unbuffered), others to stdout
            // Manually flush to ensure immediate output
            if (lvl == level::error)
            {
                std::println(stderr, "[{0}] [{1}:{2}] {3}", level_to_char(lvl), file, loc.line(),
                             std::format(fmt, std::forward<Args>(args)...));
                std::fflush(stderr);
            }
            else
            {
                std::println(stdout, "[{0}] [{1}:{2}] {3}", level_to_char(lvl), file, loc.line(),
                             std::format(fmt, std::forward<Args>(args)...));
                std::fflush(stdout);
            }
        }
        // LCOV_EXCL_START
        // Unreachable in practice, and deliberately kept: log() is noexcept, so anything escaping it
        // would call std::terminate. Every call site passes a format string checked at compile time
        // against its arguments, which leaves only an allocation failure inside std::format - and a
        // process that cannot allocate a log line has already lost.
        catch (...)
        {
            // Swallow exceptions
        }
        // LCOV_EXCL_STOP
    }

} // namespace logger
