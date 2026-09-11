/// @file src/kmx/aio/readiness/statistics.cpp
/// @brief Resetting the readiness-model executor's epoll and task counters.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/readiness/statistics.hpp>
#ifndef PCH
    #include <atomic>
#endif

namespace kmx::aio::readiness
{
    void statistics::reset() noexcept
    {
        total_registrations.store(0u, std::memory_order_relaxed);
        total_unregistrations.store(0u, std::memory_order_relaxed);
        total_epoll_waits.store(0u, std::memory_order_relaxed);
        total_events_received.store(0u, std::memory_order_relaxed);
        timeout_count.store(0u, std::memory_order_relaxed);
        error_count.store(0u, std::memory_order_relaxed);
        total_tasks_spawned.store(0u, std::memory_order_relaxed);
        total_tasks_completed.store(0u, std::memory_order_relaxed);
    }

}
