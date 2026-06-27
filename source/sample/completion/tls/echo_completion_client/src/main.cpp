#include "kmx/aio/sample/tls/echo_completion_client/manager.hpp"
#include <exception>
#include <kmx/logger.hpp>
#include <source_location>

int main() noexcept
{
    try
    {
        kmx::aio::sample::tls::echo_completion_client::manager server;
        return server.run() ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Fatal crash: {}", e.what());
        return 1;
    }
}
