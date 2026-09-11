/// @file api/kmx/aio/gpu/statistics.hpp
/// @brief Counters for GPU events, tasks and errors seen by the GPU completion-model executor.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_CUDA)
    #ifndef PCH
        #include <atomic>
    #endif

namespace kmx::aio::gpu
{
    /// @brief Statistics for GPU operations and executor performance.
    struct statistics
    {
        std::atomic_uint64_t total_events_created {};   ///< Total GPU events created.
        std::atomic_uint64_t total_events_completed {}; ///< Total GPU events signaled.
        std::atomic_uint64_t total_tasks_spawned {};    ///< Total top-level tasks spawned.
        std::atomic_uint64_t total_tasks_completed {};  ///< Total top-level tasks completed.
        std::atomic_uint64_t error_count {};            ///< Total GPU errors encountered.
        std::atomic_uint64_t poll_timeout_count {};     ///< Times event polling timed out.

        /// @brief Default constructor (move-only, deletedcopy).
        statistics() noexcept = default;

        /// @brief Non-copyable (contains atomics).
        statistics(const statistics&) = delete;
        /// @brief Non-copyable (contains atomics).
        statistics& operator=(const statistics&) = delete;

        /// @brief Resets all counters to zero.
        void reset() noexcept;
    };

}
#endif // KMX_AIO_FEATURE_CUDA
