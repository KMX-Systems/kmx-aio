#pragma once

#include <memory>
#include <kmx/aio/task.hpp>
#include <kmx/aio/completion/executor.hpp>

namespace kmx::aio::sample::quic::echo_server
{
    auto async_main(std::shared_ptr<kmx::aio::completion::executor> exec) -> kmx::aio::task<void>;
}
