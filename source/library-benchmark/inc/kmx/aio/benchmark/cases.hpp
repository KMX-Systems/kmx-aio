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

    /// @brief Records what each scenario measured on both models does.
    /// @details Call this before the per-model registrations: it fixes the order the comparison rows
    ///          come out in, which would otherwise depend on which model happened to register first.
    /// @param reg The registry to fill.
    /// @throws std::bad_alloc if the registry cannot grow.
    void register_paired_cases(registry& reg) noexcept(false);

    /// @brief Registers the TLS scenarios for whichever models this build has.
    /// @param reg The registry to fill.
    /// @throws std::bad_alloc if the registry cannot grow.
    void register_tls_cases(registry& reg) noexcept(false);

    /// @brief Registers the HTTP/2 and HTTP/3 codec cases, where those features are built.
    /// @param reg The registry to fill.
    /// @throws std::bad_alloc if the registry cannot grow.
    void register_http_cases(registry& reg) noexcept(false);

    /// @brief Registers the features the matrix gives one execution model only.
    /// @param reg The registry to fill.
    /// @throws std::bad_alloc if the registry cannot grow.
    void register_single_model_cases(registry& reg) noexcept(false);

} // namespace kmx::aio::benchmark
