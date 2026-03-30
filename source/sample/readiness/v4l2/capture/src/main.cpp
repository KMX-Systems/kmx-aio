#include <exception>
#include <source_location>
#include <string>

#include <kmx/aio/sample/v4l2/capture/manager.hpp>
#include <kmx/logger.hpp>

int main(const int argc, const char* const argv[]) noexcept
{
    try
    {
        kmx::aio::sample::v4l2::capture::config cfg;

        // Accept optional positional args: <device> <width> <height> <max_frames>
        if (argc > 1)
            cfg.device = argv[1];
        if (argc > 3)
        {
            cfg.size.width  = static_cast<std::uint32_t>(std::stoul(argv[2]));
            cfg.size.height = static_cast<std::uint32_t>(std::stoul(argv[3]));
        }
        if (argc > 4)
            cfg.max_frames = std::stoull(argv[4]);

        kmx::aio::sample::v4l2::capture::manager mgr { std::move(cfg) };
        return mgr.run() ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        kmx::logger::log(kmx::logger::level::error, std::source_location::current(),
                         "Fatal: {}", e.what());
        return 1;
    }
}
