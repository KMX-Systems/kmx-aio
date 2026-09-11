/// @file inc/kmx/aio/sample/tcp/echo/common.hpp
/// @brief Helpers shared by the TCP and TLS echo samples: random ASCII payloads and human-readable byte counts.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <string>
    #include <vector>
#endif

namespace kmx::aio::sample::tcp::echo::common
{
    /// @brief Generate a random ASCII buffer with size in [20, 500].
    void generate_random_buffer(std::vector<char>& buffer);

    /// @brief Format byte counts with dynamic units (B, KB, MB, GB, TB).
    [[nodiscard]] std::string format_bytes(const std::uint64_t bytes);
}
