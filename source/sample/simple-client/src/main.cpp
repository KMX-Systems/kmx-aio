#include "kmx/aio/sample/simple_client/manager.hpp"

int main() noexcept
{
    using namespace kmx;
    try
    {
        aio::sample::simple_client::manager stress_test;
        return stress_test.run();
    }
    catch (const std::exception& e)
    {
        logger::log(logger::level::error, std::source_location::current(), "Fatal error: {}", e.what());
        return 1;
    }
}
