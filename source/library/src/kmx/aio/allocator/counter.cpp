/// @file aio/allocator/counter.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/allocator/counter.hpp>

#include <kmx/aio/allocator/detail/thread_state.hpp>

namespace kmx::aio::allocator
{
    std::uint64_t counter::load(const std::memory_order /*order*/) const noexcept
    {
        return detail::total_allocations(kind_);
    }
} // namespace kmx::aio::allocator
