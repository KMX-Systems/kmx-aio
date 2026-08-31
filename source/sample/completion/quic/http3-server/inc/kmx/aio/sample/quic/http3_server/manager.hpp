#pragma once
#ifndef PCH
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::sample::quic::http3_server
{
    kmx::aio::task<void> async_main(kmx::aio::completion::executor& exec);
}
