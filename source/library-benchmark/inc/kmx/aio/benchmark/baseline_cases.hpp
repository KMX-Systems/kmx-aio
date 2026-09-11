/// @file inc/kmx/aio/benchmark/baseline_cases.hpp
/// @brief Registration entry point for the raw-syscall reference benchmarks.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/benchmark/registry.hpp>
#endif

namespace kmx::aio::benchmark
{
    /// @brief Registers the raw-syscall reference cases the library is measured against.
    /// @param reg The registry to fill.
    /// @throws std::bad_alloc if the registry cannot grow.
    void register_baseline_cases(registry& reg) noexcept(false);
}
