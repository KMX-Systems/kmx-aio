/// @file inc/kmx/aio/benchmark/feature/tls_cases.hpp
/// @brief Registration entry point for the TLS benchmarks.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/benchmark/registry.hpp>
#endif

namespace kmx::aio::benchmark::feature
{
    /// @brief Registers the TLS scenarios for whichever models this build has.
    /// @param reg The registry to fill.
    /// @throws std::bad_alloc if the registry cannot grow.
    void register_tls_cases(registry& reg) noexcept(false);
}
