/// @file aio/http3/alpn.hpp
/// @brief HTTP/3 ALPN definitions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <array>
#include <string_view>

namespace kmx::aio::http3::alpn
{
    /// @brief Default HTTP/3 ALPN identifier used by the demo stack.
    inline constexpr std::string_view id = "kmx-aio-h3";
    /// @brief Wire-format ALPN value for `id` in TLS negotiation.
    inline constexpr std::array<unsigned char, 11u> wire {10u, 'k', 'm', 'x', '-', 'a', 'i', 'o', '-', 'h', '3'};
} // namespace kmx::aio::http3::alpn