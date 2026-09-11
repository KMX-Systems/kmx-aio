/// @file src/main.cpp
/// @brief Entry point of the readiness-model minimal TCP client sample: runs the manager with default settings.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#ifndef PCH
    #include <kmx/aio/sample/tcp/minimal/client/manager.hpp>
    #include <kmx/logger.hpp>

    #include <exception>
    #include <source_location>
#endif

int main() noexcept
{
    try
    {
        kmx::aio::sample::tcp::minimal::client::manager client;
        return client.run() ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Fatal error: {}", e.what());
        return 1;
    }
}
