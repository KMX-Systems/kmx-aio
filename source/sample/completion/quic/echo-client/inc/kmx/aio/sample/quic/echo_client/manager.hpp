/// @file inc/kmx/aio/sample/quic/echo_client/manager.hpp
/// @brief Declares async_main() of the completion-model QUIC echo client sample.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::sample::quic::echo_client
{
    kmx::aio::task<void> async_main(kmx::aio::completion::executor& exec);
}
