#include <kmx/aio/sample/avb/listener/manager.hpp>
#include <kmx/aio/sample/common/cli_parse.hpp>
#include <kmx/aio/mac.hpp>

#include <exception>
#include <kmx/logger.hpp>
#include <print>
#include <source_location>
#include <string_view>
#include <unordered_map>

namespace kmx::aio::sample::avb::listener::detail
{
    enum class parse_status
    {
        ok,
        help,
        error,
    };

    static void print_usage(const char* program)
    {
        std::println("Usage: {} [--iface IFACE] [--talker-mac XX:XX:XX:XX:XX:XX] [--stream-id N] [--max-frames N] [--period-us N] "
                     "[--sync-timeout-s N] [--diagnostics-only]",
                     program);
        std::println("  --iface IFACE     Network interface (default: eth0)");
        std::println("  --talker-mac MAC  Talker source MAC for SRP subscribe (default: 02:00:00:00:00:01)");
        std::println("  --stream-id N     Stream unique id (0..65535, default: 1)");
        std::println("  --max-frames N    Number of frames to receive (default: 4000)");
        std::println("  --period-us N     Expected period in microseconds (1..1000000, default: 125)");
        std::println("  --sync-timeout-s N  gPTP and SRP subscribe timeout in seconds (1..300, default: 5)");
        std::println("  --diagnostics-only  Validate gPTP/SRP bring-up then exit");
        std::println("  --help            Show this help");
    }

    static parse_status parse_talker_mac_option(const std::string_view value, kmx::aio::sample::avb::listener::config& cfg)
    {
        if (!kmx::aio::parse_mac_address(value, cfg.talker_mac))
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Invalid --talker-mac value '{}'", value);
            return parse_status::error;
        }

        return parse_status::ok;
    }

    static parse_status parse_stream_id_option(const std::string_view value, kmx::aio::sample::avb::listener::config& cfg)
    {
        std::uint16_t parsed {};
        if (!kmx::aio::sample::common::parse_unsigned_u16(value, parsed))
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(),
                             "Invalid --stream-id value '{}' (expected 0..65535)", value);
            return parse_status::error;
        }

        cfg.stream_unique_id = parsed;
        return parse_status::ok;
    }

    static parse_status parse_max_frames_option(const std::string_view value, kmx::aio::sample::avb::listener::config& cfg)
    {
        std::uint64_t parsed {};
        if (!kmx::aio::sample::common::parse_unsigned_u64(value, parsed))
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Invalid --max-frames value '{}'", value);
            return parse_status::error;
        }

        cfg.max_frames = parsed;
        return parse_status::ok;
    }

    static parse_status parse_period_us_option(const std::string_view value, kmx::aio::sample::avb::listener::config& cfg,
                                               std::uint64_t min_period_us, std::uint64_t max_period_us)
    {
        std::uint64_t parsed {};
        if (!kmx::aio::sample::common::parse_unsigned_u64(value, parsed))
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

        cfg.expected_period = std::chrono::microseconds {parsed};
        return parse_status::ok;
    }

    static parse_status parse_sync_timeout_s_option(const std::string_view value, kmx::aio::sample::avb::listener::config& cfg,
                                                    std::uint64_t min_sync_timeout_s, std::uint64_t max_sync_timeout_s)
    {
        std::uint64_t parsed {};
        if (!kmx::aio::sample::common::parse_unsigned_u64(value, parsed))
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Invalid --sync-timeout-s value '{}'", value);
            return parse_status::error;
        }

        if (parsed < min_sync_timeout_s || parsed > max_sync_timeout_s)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(),
                             "Out-of-range --sync-timeout-s value '{}' (expected 1..300)", value);
            return parse_status::error;
        }

        cfg.sync_timeout = std::chrono::seconds {parsed};
        cfg.srp_subscribe_timeout = std::chrono::seconds {parsed};
        return parse_status::ok;
    }

    static parse_status parse_args(int argc, const char* argv[], kmx::aio::sample::avb::listener::config& cfg)
    {
        enum class option_kind
        {
            iface,
            talker_mac,
            stream_id,
            max_frames,
            period_us,
            sync_timeout_s,
        };

        static const std::unordered_map<std::string_view, option_kind> option_table {
            {"--iface", option_kind::iface},         {"--talker-mac", option_kind::talker_mac},
            {"--stream-id", option_kind::stream_id}, {"--max-frames", option_kind::max_frames},
            {"--period-us", option_kind::period_us}, {"--sync-timeout-s", option_kind::sync_timeout_s},
        };

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
                print_usage(argv[0] ? argv[0] : "sample-avb-listener");
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
            const auto it = option_table.find(arg);
            if (it == option_table.end())
            {
                kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Unknown argument '{}'", arg);
                return parse_status::error;
            }

            switch (it->second)
            {
                case option_kind::iface:
                {
                    cfg.iface = std::string {value};
                    break;
                }
                case option_kind::talker_mac:
                {
                    if (const auto status = parse_talker_mac_option(value, cfg); status != parse_status::ok)
                        return status;
                    break;
                }
                case option_kind::stream_id:
                {
                    if (const auto status = parse_stream_id_option(value, cfg); status != parse_status::ok)
                        return status;
                    break;
                }
                case option_kind::max_frames:
                {
                    if (const auto status = parse_max_frames_option(value, cfg); status != parse_status::ok)
                        return status;
                    break;
                }
                case option_kind::period_us:
                {
                    if (const auto status = parse_period_us_option(value, cfg, min_period_us, max_period_us); status != parse_status::ok)
                        return status;
                    break;
                }
                case option_kind::sync_timeout_s:
                {
                    if (const auto status = parse_sync_timeout_s_option(value, cfg, min_sync_timeout_s, max_sync_timeout_s);
                        status != parse_status::ok)
                        return status;
                    break;
                }
                default:;
            }
        }

        return parse_status::ok;
    }
} // namespace kmx::aio::sample::avb::listener::detail

int main(int argc, const char* argv[]) noexcept
{
    try
    {
        kmx::aio::sample::avb::listener::config cfg {};
        const auto parsed = kmx::aio::sample::avb::listener::detail::parse_args(argc, argv, cfg);
        if (parsed == kmx::aio::sample::avb::listener::detail::parse_status::help)
            return 0;
        if (parsed == kmx::aio::sample::avb::listener::detail::parse_status::error)
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
