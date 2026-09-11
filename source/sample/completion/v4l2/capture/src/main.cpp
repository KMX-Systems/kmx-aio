/// @file src/main.cpp
/// @brief Entry point of the completion-model V4L2 capture sample: runs the manager with default settings.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#ifndef PCH
    #include <kmx/aio/sample/v4l2/completion_capture/manager.hpp>
    #include <kmx/logger.hpp>

    #include <exception>
    #include <source_location>
#endif

int main() noexcept
{
    try
    {
        kmx::aio::sample::v4l2::completion_capture::manager mgr;
        return mgr.run() ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Fatal error: {}", e.what());
        return 1;
    }
}
