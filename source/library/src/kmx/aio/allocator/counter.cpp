/// @file src/kmx/aio/allocator/counter.cpp
/// @brief The compiled body of the process-wide allocation counter, summed from the per-thread blocks.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/allocator/counter.hpp>
#ifndef PCH
    #include <kmx/aio/allocator/detail/thread_state.hpp>
#endif

namespace kmx::aio::allocator
{
    std::uint64_t counter::load(const std::memory_order /*order*/) const noexcept
    {
        return detail::total_allocations(kind_);
    }
}
