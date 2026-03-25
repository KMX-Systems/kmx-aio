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
        unsigned max_streams_in {100};
        /// @brief Connection idle timeout in seconds.
        unsigned idle_conn_timeout_sec {30};
        /// @brief Max connection flow control window in bytes.
        unsigned max_cfcwnd {32 * 1024 * 1024};
    };
} // namespace kmx::aio::quic

#endif
