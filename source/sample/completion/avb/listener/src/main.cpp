#include <kmx/aio/sample/avb/listener/manager.hpp>

#include <charconv>
#include <exception>
#include <kmx/logger.hpp>
#include <print>
#include <source_location>
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
        std::println("Usage: {} [--iface IFACE] [--stream-id N] [--max-frames N] [--period-us N]", program);
        std::println("  --iface IFACE     Network interface (default: eth0)");
        std::println("  --stream-id N     Stream unique id (0..65535, default: 1)");
        std::println("  --max-frames N    Number of frames to receive (default: 4000)");
        std::println("  --period-us N     Expected period in microseconds (default: 125)");
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

    auto parse_args(int argc, const char** argv, kmx::aio::sample::avb::listener::config& cfg) -> parse_status
    {
        for (int i = 1; i < argc; ++i)
        {
            if (!argv[i])
                return parse_status::error;

            const std::string_view arg {argv[i]};
            if (arg == "--help")
            {
                print_usage(argv[0] ? argv[0] : "sample-avb-listener");
                return parse_status::help;
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
            else if (arg == "--stream-id")
            {
                std::uint16_t parsed {};
                if (!parse_unsigned(value, parsed))
                    return parse_status::error;
                cfg.stream_unique_id = parsed;
            }
            else if (arg == "--max-frames")
            {
                std::uint64_t parsed {};
                if (!parse_unsigned(value, parsed))
                    return parse_status::error;
                cfg.max_frames = parsed;
            }
            else if (arg == "--period-us")
            {
                std::uint64_t parsed {};
                if (!parse_unsigned(value, parsed))
                    return parse_status::error;
                cfg.expected_period = std::chrono::microseconds {parsed};
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
        kmx::aio::sample::avb::listener::config cfg {};
        const auto parsed = parse_args(argc, argv, cfg);
        if (parsed == parse_status::help)
            return 0;
        if (parsed == parse_status::error)
            return 1;

        kmx::aio::sample::avb::listener::manager mgr {std::move(cfg)};
        return mgr.run() ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Fatal error: {}", e.what());
        return 1;
    }
}
