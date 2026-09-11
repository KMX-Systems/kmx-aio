/// @file inc/kmx/aio/benchmark/feature/single_model_cases.hpp
/// @brief Registration entry point for the benchmarks of features the matrix gives one execution model only.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/benchmark/registry.hpp>
#endif

namespace kmx::aio::benchmark::feature
{
    /// @brief Registers the features the matrix gives one execution model only.
    /// @param reg The registry to fill.
    /// @throws std::bad_alloc if the registry cannot grow.
    void register_single_model_cases(registry& reg) noexcept(false);
}
