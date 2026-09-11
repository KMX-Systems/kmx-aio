/// @file inc/kmx/aio/sample/spdk/discovery/manager.hpp
/// @brief Completion-model SPDK bdev discovery sample: requested-name collection and discovery run declarations.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <string>
    #include <vector>
#endif

namespace kmx::aio::sample::spdk::discovery
{
    /// @brief Collects the non-empty bdev names given on the command line.
    /// @param argc Argument count, as passed to @c main.
    /// @param argv Argument vector, as passed to @c main; @c argv[0] is skipped.
    /// @return The requested bdev names, in command-line order.
    [[nodiscard]] std::vector<std::string> collect_requested(const int argc, const char* argv[]);

    /// @brief Lists the registered SPDK bdevs and probes the ones named on the command line.
    /// @param argc Argument count, as passed to @c main.
    /// @param argv Argument vector, as passed to @c main.
    /// @return 0 when bdevs are registered and, if any were requested, at least one of them could be opened; 1 otherwise.
    [[nodiscard]] int run(int argc, const char* argv[]);
}
