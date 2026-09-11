/// @file src/main.cpp
/// @brief Entry point of the completion-model SPDK bdev discovery sample: runs discovery over the command-line bdev names.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#ifndef PCH
    #include <kmx/aio/sample/spdk/discovery/manager.hpp>
    #include <kmx/logger.hpp>

    #include <exception>
    #include <source_location>
#endif

int main(int argc, const char* argv[]) noexcept
{
    try
    {
        return kmx::aio::sample::spdk::discovery::run(argc, argv);
    }
    catch (const std::exception& e)
    {
        kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Fatal error: {}", e.what());
        return 1;
    }
}
