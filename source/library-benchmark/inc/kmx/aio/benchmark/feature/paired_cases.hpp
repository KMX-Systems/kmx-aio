/// @file inc/kmx/aio/benchmark/feature/paired_cases.hpp
/// @brief Registration entry point for the catalogue of scenarios measured on both execution models.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/benchmark/registry.hpp>
#endif

namespace kmx::aio::benchmark::feature
{
    /// @brief Records what each scenario measured on both models does.
    /// @details Call this before the per-model registrations: it fixes the order the comparison rows
    ///          come out in, which would otherwise depend on which model happened to register first.
    /// @param reg The registry to fill.
    /// @throws std::bad_alloc if the registry cannot grow.
    void register_paired_cases(registry& reg) noexcept(false);
}
