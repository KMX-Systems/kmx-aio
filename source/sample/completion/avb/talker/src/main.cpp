#include <kmx/aio/sample/avb/talker/manager.hpp>

#include <charconv>
#include <exception>
#include <kmx/logger.hpp>
#include <print>
#include <source_location>
#include <sstream>
#include <string_view>

namespace
{
    enum class parse_status
    {
        ok,
        help,
        error,
    };

    auto print_usage(const char* program) -> void
    {
        std::println("Usage: {} [--iface IFACE] [--dest-mac XX:XX:XX:XX:XX:XX] [--stream-id N] [--max-frames N] [--period-us N] "
                     "[--sync-timeout-s N] [--diagnostics-only]",
                     program);
        std::println("  --iface IFACE     Network interface (default: eth0)");
        std::println("  --dest-mac MAC    Destination AVTP MAC (default: 91:E0:F0:00:0E:80)");
        std::println("  --stream-id N     Stream unique id (0..65535, default: 1)");
        std::println("  --max-frames N    Number of frames to send (default: 4000)");
        std::println("  --period-us N     Frame period in microseconds (1..1000000, default: 125)");
        std::println("  --sync-timeout-s N  gPTP sync timeout in seconds (1..300, default: 5)");
        std::println("  --diagnostics-only  Validate gPTP/SRP bring-up then exit");
        std::println("  --help            Show this help");
    }

    template <typename T>
    auto parse_unsigned(std::string_view text, T& out) -> bool
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

    auto parse_mac(std::string_view text, kmx::aio::avb::mac_address_t& out) -> bool
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

    auto parse_args(int argc, const char** argv, kmx::aio::sample::avb::talker::config& cfg) -> parse_status
    {
        static constexpr std::uint64_t min_period_us = 1u;
        static constexpr std::uint64_t max_period_us = 1'000'000u;
        static constexpr std::uint64_t min_sync_timeout_s = 1u;
        static constexpr std::uint64_t max_sync_timeout_s = 300u;

        for (int i = 1; i < argc; ++i)
        {
            if (!argv[i])
                return parse_status::error;

            const std::string_view arg {argv[i]};
            if (arg == "--help")
            {
                print_usage(argv[0] ? argv[0] : "sample-avb-talker");
                return parse_status::help;
            }

            if (arg == "--diagnostics-only")
            {
                cfg.diagnostics_only = true;
                continue;
            }

            if (i + 1 >= argc || !argv[i + 1])
            {
                kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Missing value for argument '{}'", arg);
                return parse_status::error;
            }

            const std::string_view value {argv[++i]};
            if (arg == "--iface")
            {
                cfg.iface = std::string {value};
            }
            else if (arg == "--dest-mac")
            {
                if (!parse_mac(value, cfg.dest_mac))
                {
                    kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Invalid --dest-mac value '{}'", value);
                    return parse_status::error;
                }
            }
            else if (arg == "--stream-id")
            {
                std::uint16_t parsed {};
                if (!parse_unsigned(value, parsed))
                {
                    kmx::logger::log(kmx::logger::level::error, std::source_location::current(),
                                     "Invalid --stream-id value '{}' (expected 0..65535)", value);
                    return parse_status::error;
                }
                cfg.stream_unique_id = parsed;
            }
            else if (arg == "--max-frames")
            {
                std::uint64_t parsed {};
                if (!parse_unsigned(value, parsed))
                {
                    kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Invalid --max-frames value '{}'", value);
                    return parse_status::error;
                }
                cfg.max_frames = parsed;
            }
            else if (arg == "--period-us")
            {
                std::uint64_t parsed {};
                if (!parse_unsigned(value, parsed))
                {
                    kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Invalid --period-us value '{}'", value);
                    return parse_status::error;
                }
                if (parsed < min_period_us || parsed > max_period_us)
                {
                    kmx::logger::log(kmx::logger::level::error, std::source_location::current(),
                                     "Out-of-range --period-us value '{}' (expected 1..1000000)", value);
                    return parse_status::error;
                }
                cfg.frame_period = std::chrono::microseconds {parsed};
            }
            else if (arg == "--sync-timeout-s")
            {
                std::uint64_t parsed {};
                if (!parse_unsigned(value, parsed))
                {
                    kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Invalid --sync-timeout-s value '{}'",
                                     value);
                    return parse_status::error;
                }
                if (parsed < min_sync_timeout_s || parsed > max_sync_timeout_s)
                {
                    kmx::logger::log(kmx::logger::level::error, std::source_location::current(),
                                     "Out-of-range --sync-timeout-s value '{}' (expected 1..300)", value);
                    return parse_status::error;
                }
                cfg.sync_timeout = std::chrono::seconds {parsed};
            }
            else
            {
                kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Unknown argument '{}'", arg);
                return parse_status::error;
            }
        }

        return parse_status::ok;
    }
} // namespace

int main(int argc, const char** argv) noexcept
{
    try
    {
        kmx::aio::sample::avb::talker::config cfg {};
        const auto parsed = parse_args(argc, argv, cfg);
        if (parsed == parse_status::help)
            return 0;
        if (parsed == parse_status::error)
            return 1;

        kmx::aio::sample::avb::talker::manager mgr {std::move(cfg)};
        return mgr.run() ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Fatal error: {}", e.what());
        return 1;
    }
}
