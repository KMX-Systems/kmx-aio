/// @file aio/allocator/counter.hpp
/// @brief Process-wide allocation total summed from the per-thread counters when read.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <atomic>
    #include <cstdint>
#endif

namespace kmx::aio::allocator
{
    /// @brief Which of the per-thread counters a process-wide total refers to.
    enum class counter_kind : std::uint8_t
    {
        slab, ///< Frames served from a thread's slab.
        heap  ///< Frames served from the heap.
    };

    /// @brief A process-wide allocation total, summed from the per-thread counters when read.
    /// @details Reads like the atomic counter it replaces - `load()` with an optional memory order -
    ///          but there is no single location being read: the count is spread across the threads that
    ///          did the allocating, which is what keeps the allocation path free of shared writes.
    class counter
    {
    public:
        /// @brief Binds the total to one of the per-thread counters.
        /// @param kind Which counter this total sums.
        explicit constexpr counter(const counter_kind kind) noexcept: kind_(kind) {}

        /// @brief Sums the counter across every thread that has allocated a coroutine frame.
        /// @param order Ignored; accepted so this reads like the atomic it replaces.
        /// @return The process-wide total.
        [[nodiscard]] std::uint64_t load(const std::memory_order order = std::memory_order_relaxed) const noexcept;

    private:
        /// @brief Which per-thread counter is summed.
        counter_kind kind_;
    };

} // namespace kmx::aio::allocator
