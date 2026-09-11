/// @file src/main.cpp
/// @brief Entry point of the readiness-model QUIC echo server sample: spawns async_main on a readiness executor.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#ifndef PCH
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/sample/quic/echo_server/manager.hpp>

    #include <exception>
    #include <iostream>
    #include <memory>
#endif

using namespace kmx::aio;
using namespace kmx::aio::readiness;

int main()
{
    try
    {
        auto exec = std::make_shared<executor>();
        exec->spawn(kmx::aio::sample::quic::echo_server::async_main(exec));
        exec->run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
