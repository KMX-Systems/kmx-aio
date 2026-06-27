#include <atomic>
#include <exception>
#include <memory>
#include <source_location>
#include <string>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/logger.hpp>

#include <kmx/aio/sample/xdp/packet_filter/manager.hpp>

int main(int argc, const char** argv) noexcept
{
    if (argc < 2)
    {
        kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Usage: {} <interface> [queue_id]", argv[0]);
        return 1;
    }

    try
    {
        std::uint32_t queue_id = 0u;
        if (argc >= 3)
            queue_id = static_cast<std::uint32_t>(std::stoul(argv[2]));

        auto exec = std::make_shared<kmx::aio::completion::executor>();
        auto ok = std::make_shared<std::atomic_bool>(false);

        exec->spawn(kmx::aio::sample::xdp::packet_filter::run_packet_filter(exec, ok, std::string(argv[1]), queue_id));
        exec->run();

        return ok->load(std::memory_order_relaxed) ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Fatal error: {}", e.what());
        return 1;
    }
}
