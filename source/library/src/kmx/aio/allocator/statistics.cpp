/// @file aio/allocator/statistics.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/allocator/statistics.hpp>

#include <kmx/aio/allocator/detail/thread_state.hpp>

namespace kmx::aio::allocator
{
    /// @brief The one set of process-wide totals every reader shares.
    statistics g_statistics {};

    void statistics::reset() noexcept
    {
        detail::reset_allocations();
    }
} // namespace kmx::aio::allocator

namespace kmx::aio
{
    allocator::statistics& get_allocator_statistics() noexcept
    {
        return allocator::g_statistics;
    }
} // namespace kmx::aio
