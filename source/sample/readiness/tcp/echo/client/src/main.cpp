#include <exception>
#include <kmx/aio/sample/tcp/echo/client/manager.hpp>
#include <kmx/logger.hpp>
#include <source_location>

int main() noexcept
{
    try
    {
        kmx::aio::sample::tcp::echo::client::manager client;
        return client.run() ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Fatal error: {}", e.what());
        return 1;
    }
}
