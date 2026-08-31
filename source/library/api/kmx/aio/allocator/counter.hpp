/// @file aio/allocator/counter.hpp
/// @brief Process-wide allocation total summed from the per-thread counters when read.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/allocator/detail/counter_kind.hpp>

    #include <atomic>
    #include <cstdint>
#endif

namespace kmx::aio::allocator
{
    /// @brief A process-wide allocation total, summed from the per-thread counters when read.
    /// @details Reads like the atomic counter it replaces - `load()` with an optional memory order -
    ///          but there is no single location being read: the count is spread across the threads that
    ///          did the allocating, which is what keeps the allocation path free of shared writes.
    class counter
    {
    public:
        /// @brief Binds the total to one of the per-thread counters.
        /// @param kind Which counter this total sums.
        explicit constexpr counter(const detail::counter_kind kind) noexcept: kind_(kind) {}

        /// @brief Sums the counter across every thread that has allocated a coroutine frame.
        /// @param order Ignored; accepted so this reads like the atomic it replaces.
        /// @return The process-wide total.
        [[nodiscard]] std::uint64_t load(const std::memory_order order = std::memory_order_relaxed) const noexcept;

    private:
        /// @brief Which per-thread counter is summed.
        detail::counter_kind kind_;
    };

} // namespace kmx::aio::allocator
