#pragma once

#include <memory>
#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::sample::quic::http3_client
{
    auto async_main(std::shared_ptr<kmx::aio::completion::executor> exec) -> kmx::aio::task<void>;
}
