/// @file src/kmx/aio/completion/statistics.cpp
/// @brief Resetting the completion-model executor's io_uring and task counters.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/completion/statistics.hpp>
#ifndef PCH
    #include <atomic>
#endif

namespace kmx::aio::completion
{
    void statistics::reset() noexcept
    {
        total_submissions.store(0u, std::memory_order_relaxed);
        total_completions.store(0u, std::memory_order_relaxed);
        total_tasks_spawned.store(0u, std::memory_order_relaxed);
        total_tasks_completed.store(0u, std::memory_order_relaxed);
        error_count.store(0u, std::memory_order_relaxed);
        submission_full_count.store(0u, std::memory_order_relaxed);
    }

}
