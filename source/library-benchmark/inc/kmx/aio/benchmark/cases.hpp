/// @file aio/benchmark/cases.hpp
/// @brief Registration entry points for the benchmark groups.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/benchmark/harness.hpp>
#endif

namespace kmx::aio::benchmark
{
    /// @brief Registers the coroutine, allocator, channel and buffer-pool cases.
    /// @param reg The registry to fill.
    /// @throws std::bad_alloc if the registry cannot grow.
    void register_core_cases(registry& reg) noexcept(false);

    /// @brief Registers the raw-syscall reference cases the library is measured against.
    /// @param reg The registry to fill.
    /// @throws std::bad_alloc if the registry cannot grow.
    void register_baseline_cases(registry& reg) noexcept(false);

    /// @brief Registers the readiness-executor cases.
    /// @param reg The registry to fill.
    /// @throws std::bad_alloc if the registry cannot grow.
    void register_readiness_cases(registry& reg) noexcept(false);

    /// @brief Registers the completion-executor cases.
    /// @param reg The registry to fill.
    /// @throws std::bad_alloc if the registry cannot grow.
    void register_completion_cases(registry& reg) noexcept(false);

} // namespace kmx::aio::benchmark
