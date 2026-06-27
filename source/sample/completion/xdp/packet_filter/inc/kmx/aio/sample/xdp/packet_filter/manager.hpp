#pragma once

#include <atomic>
#include <memory>
#include <string>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::sample::xdp::packet_filter
{
    auto run_packet_filter(std::shared_ptr<kmx::aio::completion::executor> exec, std::shared_ptr<std::atomic_bool> ok,
                           std::string interface_name, std::uint32_t queue_id) -> kmx::aio::task<void>;
}
