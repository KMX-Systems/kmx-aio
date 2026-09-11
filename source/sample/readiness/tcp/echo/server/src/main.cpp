/// @file src/main.cpp
/// @brief Entry point of the readiness-model TCP echo server sample: runs the manager with default settings.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#ifndef PCH
    #include <kmx/aio/sample/tcp/echo/server/manager.hpp>
    #include <kmx/logger.hpp>

    #include <exception>
    #include <source_location>
#endif

int main() noexcept
{
    try
    {
        kmx::aio::sample::tcp::echo::server::manager server;
        return server.run() ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Fatal error: {}", e.what());
        return 1;
    }
}
