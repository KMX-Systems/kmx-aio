/// @file src/main.cpp
/// @brief Entry point of the completion-model HTTP/3 client sample: runs async_main() on an executor.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#ifndef PCH
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/sample/quic/http3_client/manager.hpp>

    #include <exception>
    #include <iostream>
#endif

using namespace kmx::aio;
using namespace kmx::aio::completion;

int main()
{
    try
    {
        executor exec;
        exec.spawn(kmx::aio::sample::quic::http3_client::async_main(exec));
        exec.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
