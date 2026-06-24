/// @file aio/quic/settings.hpp
/// @brief Common settings for QUIC engines.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#if defined(KMX_AIO_FEATURE_QUIC)

namespace kmx::aio::quic
{
    /// @brief Configuration settings for the QUIC protocol engine.
    struct settings
    {
        /// @brief Maximum concurrent streams permitted in a single connection.
        unsigned max_streams_in {100u};
        /// @brief Connection idle timeout in seconds.
        unsigned idle_conn_timeout_sec {30u};
        /// @brief Max connection flow control window in bytes.
        unsigned max_cfcwnd {32u * 1024u * 1024u}; // 32 MiB
    };
} // namespace kmx::aio::quic

#endif
