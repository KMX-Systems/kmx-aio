/// @file src/kmx/aio/gpu/statistics.cpp
/// @brief Resetting the GPU executor's event, task and error counters.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/gpu/statistics.hpp>
#ifndef PCH
    #include <atomic>
#endif

namespace kmx::aio::gpu
{
    /// Statistics Implementation

    void statistics::reset() noexcept
    {
        total_events_created.store(0u, std::memory_order_relaxed);
        total_events_completed.store(0u, std::memory_order_relaxed);
        total_tasks_spawned.store(0u, std::memory_order_relaxed);
        total_tasks_completed.store(0u, std::memory_order_relaxed);
        error_count.store(0u, std::memory_order_relaxed);
        poll_timeout_count.store(0u, std::memory_order_relaxed);
    }

}
