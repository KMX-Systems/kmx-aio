/// @file inc/kmx/aio/benchmark/readiness_cases.hpp
/// @brief Registration entry point for the readiness-executor (epoll) benchmarks.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/benchmark/registry.hpp>
#endif

namespace kmx::aio::benchmark
{
    /// @brief Registers the readiness-executor cases.
    /// @param reg The registry to fill.
    /// @throws std::bad_alloc if the registry cannot grow.
    void register_readiness_cases(registry& reg) noexcept(false);
}
