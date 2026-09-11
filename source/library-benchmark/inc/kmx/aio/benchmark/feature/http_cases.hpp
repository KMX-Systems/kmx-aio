/// @file inc/kmx/aio/benchmark/feature/http_cases.hpp
/// @brief Registration entry point for the HTTP/2 and HTTP/3 codec benchmarks.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/benchmark/registry.hpp>
#endif

namespace kmx::aio::benchmark::feature
{
    /// @brief Registers the HTTP/2 and HTTP/3 codec cases, where those features are built.
    /// @param reg The registry to fill.
    /// @throws std::bad_alloc if the registry cannot grow.
    void register_http_cases(registry& reg) noexcept(false);
}
