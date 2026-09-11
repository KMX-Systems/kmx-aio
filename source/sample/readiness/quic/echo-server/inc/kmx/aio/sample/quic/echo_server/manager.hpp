/// @file inc/kmx/aio/sample/quic/echo_server/manager.hpp
/// @brief Declares async_main for the readiness-model QUIC echo server sample.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/task.hpp>

    #include <memory>
#endif

namespace kmx::aio::sample::quic::echo_server
{
    kmx::aio::task<void> async_main(std::shared_ptr<kmx::aio::readiness::executor> exec);
}
