/// @file api/kmx/aio/completion/statistics.hpp
/// @brief Counters for io_uring operations and completion-model executor performance.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_COMPLETION)
    #ifndef PCH
        #include <atomic>
    #endif

namespace kmx::aio::completion
{
    /// @brief Statistics for io_uring operations and executor performance.
    struct statistics
    {
        std::atomic_uint64_t total_submissions {};     ///< Total SQEs submitted.
        std::atomic_uint64_t total_completions {};     ///< Total CQEs reaped.
        std::atomic_uint64_t total_tasks_spawned {};   ///< Total top-level tasks spawned.
        std::atomic_uint64_t total_tasks_completed {}; ///< Total top-level tasks completed.
        std::atomic_uint64_t error_count {};           ///< Total errors encountered.
        std::atomic_uint64_t submission_full_count {}; ///< Times the SQ was full.

        /// @brief Resets all counters to zero.
        void reset() noexcept;
    };

}
#endif // KMX_AIO_FEATURE_COMPLETION
