/// @file inc/kmx/aio/benchmark/feature/detail/run_window.hpp
/// @brief The window in which a scenario's connections were actually running.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/benchmark/harness.hpp>

    #include <atomic>
    #include <cstddef>
#endif

namespace kmx::aio::benchmark::feature::detail
{
    /// @brief The window in which the connections were actually running.
    /// @details Timing around run() would fold the executor's own start-up and its loop's final
    ///          wait timeout into the per-operation figure - on the completion side that wait is
    ///          100 ms, which spread over a few thousand operations is most of what the case would
    ///          report. A scenario that drives an executor times its own work instead: from the
    ///          first connection starting to the last one finishing.
    struct run_window
    {
        std::atomic_size_t started {};  ///< Connections that have begun.
        std::atomic_size_t finished {}; ///< Connections that have ended.
        clock_t::time_point begin {};   ///< When the first one began.
        clock_t::time_point end {};     ///< When the last one ended.

        /// @brief Marks a connection as started, stamping the window's opening on the first.
        void open() noexcept
        {
            if (started.fetch_add(1u, std::memory_order_relaxed) == 0u)
                begin = clock_t::now();
        }

        /// @brief Marks a connection as finished, stamping the window's close on the last.
        /// @param total How many connections there are in all.
        void close(const std::size_t total) noexcept
        {
            if ((finished.fetch_add(1u, std::memory_order_relaxed) + 1u) == total)
                end = clock_t::now();
        }
    };
}
