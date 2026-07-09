#pragma once

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/task.hpp>
#include <memory>

namespace kmx::aio::sample::quic::echo_client
{
    kmx::aio::task<void> async_main(std::shared_ptr<kmx::aio::completion::executor> exec) ;
}
