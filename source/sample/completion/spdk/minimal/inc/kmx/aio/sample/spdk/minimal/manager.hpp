/// @file inc/kmx/aio/sample/spdk/minimal/manager.hpp
/// @brief Completion-model SPDK minimal sample: bdev probe coroutine declaration.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/task.hpp>

    #include <atomic>
    #include <memory>
    #include <string>
#endif

namespace kmx::aio::sample::spdk::minimal
{
    /// @brief Writes one block to a bdev, reads it back and flushes, then stops the executor.
    /// @param exec The executor the probe runs on; stopped when the probe ends.
    /// @param ok Set when the block read back matches the one written.
    /// @param bdev_name The bdev to probe; @c kmx-spdk-fallback selects the in-memory device.
    /// @return The probe task.
    kmx::aio::task<void> run_probe(kmx::aio::completion::executor& exec, std::shared_ptr<std::atomic_bool> ok, std::string bdev_name);
}
