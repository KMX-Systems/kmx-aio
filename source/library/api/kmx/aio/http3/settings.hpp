/// @file aio/http3/settings.hpp
/// @brief HTTP/3 settings definitions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <cstdint>

namespace kmx::aio::http3
{
    /// @brief Minimal HTTP/3 settings model for current API and demos.
    struct settings
    {
        /// @brief QPACK maximum table capacity.
        std::uint64_t qpack_max_table_capacity {0u};
        /// @brief Maximum field section size.
        std::uint64_t max_field_section_size {16u * 1024u};
        /// @brief Number of QPACK blocked streams.
        std::uint64_t qpack_blocked_streams {0u};
        /// @brief Whether CONNECT protocol support is enabled.
        bool enable_connect_protocol {false};
        /// @brief Whether H3 datagrams are enabled.
        bool h3_datagram {false};
    };
} // namespace kmx::aio::http3