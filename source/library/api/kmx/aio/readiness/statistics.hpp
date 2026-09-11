/// @file api/kmx/aio/readiness/statistics.hpp
/// @brief Counters for epoll operations and readiness-model executor performance.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_READINESS)
    #ifndef PCH
        #include <atomic>
    #endif

namespace kmx::aio::readiness
{
    /// @brief Statistics for epoll operations and executor performance.
    struct statistics
    {
        /// @brief Number of descriptors registered since the last reset.
        std::atomic_uint64_t total_registrations {};
        /// @brief Number of descriptors unregistered since the last reset.
        std::atomic_uint64_t total_unregistrations {};
        /// @brief Number of `epoll_wait` calls issued since the last reset.
        std::atomic_uint64_t total_epoll_waits {};
        /// @brief Number of events reaped from `epoll_wait` since the last reset.
        std::atomic_uint64_t total_events_received {};
        /// @brief Number of `epoll_wait` calls that returned without an event.
        std::atomic_uint64_t timeout_count {};
        /// @brief Number of failed epoll operations since the last reset.
        std::atomic_uint64_t error_count {};
        /// @brief Number of root tasks handed to @ref executor::spawn since the last reset.
        std::atomic_uint64_t total_tasks_spawned {};
        /// @brief Number of spawned root tasks that have run to completion.
        std::atomic_uint64_t total_tasks_completed {};

        /// @brief Reset all statistics counters.
        void reset() noexcept;
    };

}
#endif // KMX_AIO_FEATURE_READINESS
