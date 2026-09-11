/// @file src/kmx/aio/allocator/statistics.cpp
/// @brief The compiled body of the process-wide coroutine-frame allocation statistics.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/allocator/statistics.hpp>
#ifndef PCH
    #include <kmx/aio/allocator/detail/thread_state.hpp>
#endif

namespace kmx::aio::allocator
{
    /// @brief The one set of process-wide totals every reader shares.
    statistics g_statistics {};

    void statistics::reset() noexcept
    {
        detail::reset_allocations();
    }

    statistics& get_statistics() noexcept
    {
        return g_statistics;
    }
}
