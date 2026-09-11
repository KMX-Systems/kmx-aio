/// @file inc/kmx/aio/benchmark/detail/pair_sides.hpp
/// @brief The two sides of one paired scenario, as the comparison report finds them among the results.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/benchmark/harness.hpp>
#endif

namespace kmx::aio::benchmark::detail
{
    /// @brief The two sides of one scenario, either of which may be absent.
    struct pair_sides
    {
        /// @brief The epoll side, or null when it was not run.
        const result* readiness {};
        /// @brief The io_uring side, or null when it was not run.
        const result* completion {};

        /// @brief Whether both sides ran to completion.
        [[nodiscard]] bool both_ran() const noexcept
        {
            return (readiness != nullptr) && (completion != nullptr) && !readiness->skipped && !completion->skipped;
        }
    };
}
