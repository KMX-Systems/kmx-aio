/// @file src/main.cpp
/// @brief Entry point of the completion-model TLS echo stress client sample: runs the manager with default settings.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#ifndef PCH
    #include <kmx/aio/sample/tls/echo_completion_client/manager.hpp>
    #include <kmx/logger.hpp>

    #include <exception>
    #include <source_location>
#endif

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
