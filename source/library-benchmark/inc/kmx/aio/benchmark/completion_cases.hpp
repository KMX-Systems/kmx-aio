/// @file inc/kmx/aio/benchmark/completion_cases.hpp
/// @brief Registration entry point for the completion-executor (io_uring) benchmarks.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/benchmark/registry.hpp>
#endif

namespace kmx::aio::benchmark
{
    /// @brief Registers the completion-executor cases.
    /// @param reg The registry to fill.
    /// @throws std::bad_alloc if the registry cannot grow.
    void register_completion_cases(registry& reg) noexcept(false);
}
