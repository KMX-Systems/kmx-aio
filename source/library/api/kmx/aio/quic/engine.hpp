/// @file api/kmx/aio/quic/engine.hpp
/// @brief The peer a client QUIC engine connects to, shared by the generic engine and its implementation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_QUIC)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/quic/settings.hpp>

        #include <string>
        #include <vector>
    #endif

namespace kmx::aio::quic
{
    /// @brief The peer a client engine connects to, what it sends there, and how the connection is set up.
    struct connect_params
    {
        /// @brief IP address to connect to; a view, so the storage it refers to must outlive the connect.
        ip_address_t peer_ip;
        /// @brief Port number to connect to.
        port_t peer_port {};
        /// @brief Hostname for SNI (Server Name Indication); empty to omit SNI.
        std::string hostname {};
        /// @brief Payloads queued client-side; each non-empty payload is written on a distinct stream.
        std::vector<std::string> payloads {};
        /// @brief BoringSSL SSL_CTX pointer; borrowed, not owned.
        void* ssl_ctx {};
        /// @brief QUIC protocol settings.
        settings config {};
    };
}

#endif // KMX_AIO_FEATURE_QUIC
