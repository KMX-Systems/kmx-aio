/// @file inc/kmx/aio/sample/quic/http3_server/manager.hpp
/// @brief Declares async_main() of the completion-model HTTP/3 server sample.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::sample::quic::http3_server
{
    kmx::aio::task<void> async_main(kmx::aio::completion::executor& exec);
}
