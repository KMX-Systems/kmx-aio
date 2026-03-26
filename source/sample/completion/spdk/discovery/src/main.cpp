#include <exception>
#include <source_location>

#include <kmx/logger.hpp>
#include <kmx/aio/sample/spdk/discovery/manager.hpp>

int main(int argc, const char** argv) noexcept
{
    try
    {
        return kmx::aio::sample::spdk::discovery::run_discovery(argc, argv);
    }
    catch (const std::exception& e)
    {
        kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Fatal error: {}", e.what());
        return 1;
    }
}
