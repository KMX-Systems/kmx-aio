#include <kmx/aio/sample/tcp/echo_uring/server/manager.hpp>
#include <kmx/logger.hpp>

#include <exception>
#include <source_location>

int main() noexcept
{
    try
    {
        kmx::aio::sample::tcp::echo_uring::server::manager server;
        return server.run() ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Fatal error: {}", e.what());
        return 1;
    }
}
