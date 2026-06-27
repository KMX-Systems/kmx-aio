#pragma once

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/task.hpp>
#include <memory>

namespace kmx::aio::sample::quic::http3_server
{
    auto async_main(std::shared_ptr<kmx::aio::completion::executor> exec) -> kmx::aio::task<void>;
}
