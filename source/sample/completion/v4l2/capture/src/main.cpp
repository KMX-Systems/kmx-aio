#include <kmx/aio/sample/v4l2/completion_capture/manager.hpp>

#include <exception>
#include <kmx/logger.hpp>
#include <source_location>

int main() noexcept
{
    try
    {
        kmx::aio::sample::v4l2::completion_capture::manager mgr;
        return mgr.run() ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Fatal error: {}", e.what());
        return 1;
    }
}
