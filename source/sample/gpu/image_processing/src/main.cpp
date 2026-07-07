#include <kmx/aio/sample/gpu/image_processing/manager.hpp>

#include <charconv>
#include <exception>
#include <iostream>
#include <limits>
#include <string_view>

namespace kmx::aio::sample::gpu::image_processing::detail
{
    auto print_usage() -> void
    {
        std::cout << "Usage: sample-gpu-image-processing [options]\n"
                     "  --device <path>         V4L2 device (default: /dev/video0)\n"
                     "  --max-frames <n>        Number of frames to process (default: 2)\n"
                     "  --width <n>             Frame width (default: 640)\n"
                     "  --height <n>            Frame height (default: 480)\n"
                     "  --buffer-count <n>      V4L2 MMAP buffer count (default: 4)\n"
                     "  --gpu-device <n>        CUDA device index (default: 0)\n"
                     "  --help                  Show this help\n";
    }

    template <typename T>
    auto parse_unsigned_arg(const char* raw, T& out) -> bool
    {
        std::uint64_t parsed {};
        const auto* begin = raw;
        const auto* end = raw + std::char_traits<char>::length(raw);
        const auto [ptr, ec] = std::from_chars(begin, end, parsed);
        if (ec != std::errc() || ptr != end)
            return false;

        if (parsed > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
            return false;

        out = static_cast<T>(parsed);
        return true;
    }

    auto parse_args(const int argc, char** argv, kmx::aio::sample::gpu::image_processing::config& cfg,
                    bool& help_requested) -> bool
    {
        help_requested = false;

        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg {argv[i]};
            if (arg == "--help")
            {
                print_usage();
                help_requested = true;
                return false;
            }

            if (i + 1 >= argc)
            {
                std::cerr << "Missing value for option: " << arg << "\n";
                return false;
            }

            const char* const value = argv[++i];
            if (arg == "--device")
            {
                cfg.device = value;
                continue;
            }

            if (arg == "--max-frames")
            {
                if (!parse_unsigned_arg(value, cfg.max_frames))
                    return false;
                continue;
            }

            if (arg == "--width")
            {
                if (!parse_unsigned_arg(value, cfg.size.width))
                    return false;
                continue;
            }

            if (arg == "--height")
            {
                if (!parse_unsigned_arg(value, cfg.size.height))
                    return false;
                continue;
            }

            if (arg == "--buffer-count")
            {
                if (!parse_unsigned_arg(value, cfg.buffer_count))
                    return false;
                continue;
            }

            if (arg == "--gpu-device")
            {
                std::uint64_t tmp {};
                if (!parse_unsigned_arg(value, tmp))
                    return false;
                if (tmp > static_cast<std::uint64_t>(std::numeric_limits<std::int16_t>::max()))
                    return false;
                cfg.gpu_device = static_cast<std::int16_t>(tmp);
                continue;
            }

            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        }

        return true;
    }
} // namespace kmx::aio::sample::gpu::image_processing::detail

int main(const int argc, char** argv) noexcept
{
    try
    {
        kmx::aio::sample::gpu::image_processing::config cfg {};
        bool help_requested = false;
        if (!kmx::aio::sample::gpu::image_processing::detail::parse_args(argc, argv, cfg, help_requested))
            return help_requested ? 0 : 1;

        kmx::aio::sample::gpu::image_processing::manager mgr {std::move(cfg)};
        return mgr.run() ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
