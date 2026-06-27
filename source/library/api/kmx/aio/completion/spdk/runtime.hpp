/// @file aio/completion/spdk/runtime.hpp
/// @brief SPDK runtime initialization and bdev enumeration helpers.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <expected>
    #include <string>
    #include <system_error>
    #include <vector>
#endif

namespace kmx::aio::completion::spdk::runtime
{
    /// @brief Initializes SPDK env and subsystems once per process.
    /// @return Success or an error code describing initialization failure.
    [[nodiscard]] std::expected<void, std::error_code> initialize() noexcept;

    /// @brief Finalizes SPDK env and subsystems cleanly gracefully stopping threads.
    /// @return Success or an error code.
    [[nodiscard]] std::expected<void, std::error_code> finalize() noexcept;

    /// @brief Enumerates currently registered SPDK bdev names.
    /// @return Vector of bdev names, or an initialization/probe error.
    [[nodiscard]] std::expected<std::vector<std::string>, std::error_code> enumerate_bdevs() noexcept;

} // namespace kmx::aio::completion::spdk::runtime
