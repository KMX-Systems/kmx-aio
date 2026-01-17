#include "kmx/aio/sample/simple_server/manager.hpp"

int main() noexcept
{
    using namespace kmx;
    try
    {
        aio::sample::simple_server::manager server;
        return server.run();
    }
    catch (const std::exception& e)
    {
        logger::log(logger::level::error, std::source_location::current(), "Fatal error: {}", e.what());
        return 1;
    }
}
