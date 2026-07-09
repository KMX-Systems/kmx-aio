#pragma once

#include <atomic>
#include <memory>
#include <string>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::sample::spdk::minimal
{
    kmx::aio::task<void> run_spdk_probe(std::shared_ptr<kmx::aio::completion::executor> exec, std::shared_ptr<std::atomic_bool> ok,
                                        std::string bdev_name);
}
