#include <kmx/aio/sample/gpu/image_processing/manager.hpp>
#include <kmx/aio/sample/common/cli_parse.hpp>

#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace kmx::aio::sample::gpu::image_processing::detail
{
    void print_usage()
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

    bool parse_gpu_device_option(const char* value, std::int16_t& out)
    {
        std::uint64_t tmp {};
        if (!kmx::aio::sample::common::parse_unsigned_u64_cstr(value, tmp))
            return false;

        if (tmp > static_cast<std::uint64_t>(std::numeric_limits<std::int16_t>::max()))
            return false;

        out = static_cast<std::int16_t>(tmp);
        return true;
    }

    bool parse_args(const int argc, char* argv[], kmx::aio::sample::gpu::image_processing::config& cfg,
                    bool& help_requested)
    {
        enum class option_kind
        {
            device,
            max_frames,
            width,
            height,
            buffer_count,
            gpu_device,
        };

        static const std::unordered_map<std::string_view, option_kind> option_table {
            {"--device", option_kind::device},
            {"--max-frames", option_kind::max_frames},
            {"--width", option_kind::width},
            {"--height", option_kind::height},
            {"--buffer-count", option_kind::buffer_count},
            {"--gpu-device", option_kind::gpu_device},
        };

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
            const auto it = option_table.find(arg);
            if (it == option_table.end())
            {
                std::cerr << "Unknown option: " << arg << "\n";
                return false;
            }

            switch (it->second)
            {
                case option_kind::device:
                    cfg.device = value;
                    break;
                case option_kind::max_frames:
                    if (!kmx::aio::sample::common::parse_unsigned_u64_cstr(value, cfg.max_frames))
                        return false;
                    break;
                case option_kind::width:
                    if (!kmx::aio::sample::common::parse_unsigned_u32_cstr(value, cfg.size.width))
                        return false;
                    break;
                case option_kind::height:
                    if (!kmx::aio::sample::common::parse_unsigned_u32_cstr(value, cfg.size.height))
                        return false;
                    break;
                case option_kind::buffer_count:
                    if (!kmx::aio::sample::common::parse_unsigned_u16_cstr(value, cfg.buffer_count))
                        return false;
                    break;
                case option_kind::gpu_device:
                    if (!parse_gpu_device_option(value, cfg.gpu_device))
                        return false;
                    break;
                default:
                    std::cerr << "Unknown option kind\n";
                    return false;
            }
        }

        return true;
    }
} // namespace kmx::aio::sample::gpu::image_processing::detail

int main(const int argc, char* argv[]) noexcept
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
