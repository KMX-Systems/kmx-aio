/// @file aio/allocator/detail/counter_kind.hpp
/// @brief Selector naming which per-thread allocation counter a process-wide total sums.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif

namespace kmx::aio::allocator::detail
{
    /// @brief Which of the per-thread counters a process-wide total refers to.
    enum class counter_kind : std::uint8_t
    {
        slab, ///< Frames served from a thread's slab.
        heap  ///< Frames served from the heap.
    };
} // namespace kmx::aio::allocator::detail
