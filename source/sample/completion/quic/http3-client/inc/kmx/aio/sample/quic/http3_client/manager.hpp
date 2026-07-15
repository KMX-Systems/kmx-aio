#pragma once

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/task.hpp>
#include <memory>

namespace kmx::aio::sample::quic::http3_client
{
    kmx::aio::task<void> async_main(kmx::aio::completion::executor& exec);
}
