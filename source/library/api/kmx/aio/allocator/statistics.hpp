/// @file aio/allocator/statistics.hpp
/// @brief Process-wide counters describing coroutine-frame allocation routing.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/allocator/counter.hpp>
#endif

namespace kmx::aio::allocator
{
    /// @brief Which of the per-thread counters a process-wide total refers to.
    enum class counter_kind : std::uint8_t;

    /// @brief Process-wide counters describing coroutine-frame allocation routing.
    /// @details Each total is summed from the per-thread counters at the moment it is read, so a
    ///          reference kept from an earlier call keeps reporting current values.
    struct statistics
    {
        /// @brief Coroutine frames served from a thread-local slab.
        counter slab_allocations {counter_kind::slab};
        /// @brief Coroutine frames that fell back to the global heap.
        counter heap_allocations {counter_kind::heap};

        /// @brief Zeroes the totals, including the counts every thread is holding.
        void reset() noexcept;
    };

} // namespace kmx::aio::allocator

namespace kmx::aio
{
    /// @brief Returns the process-wide allocator statistics.
    [[nodiscard]] allocator::statistics& get_allocator_statistics() noexcept;

} // namespace kmx::aio
