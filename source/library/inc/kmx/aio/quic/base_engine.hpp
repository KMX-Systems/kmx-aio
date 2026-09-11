/// @file inc/kmx/aio/quic/base_engine.hpp
/// @brief Helpers and server start parameters shared by the readiness and completion QUIC engine implementations.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details This is a PRIVATE implementation detail — included only from the .cpp files.
///          It must NOT appear in any public header to avoid exposing lsquic.h to consumers.
#pragma once
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/quic/settings.hpp>
    #include <kmx/logger.hpp>

    #include <lsquic.h>

    #include <string_view>
#endif

namespace kmx::aio::quic
{
    namespace logger = ::kmx::logger;

    namespace detail
    {
        /// @brief Reads the readiness-model watchdog tick period from the `KMX_AIO_QUIC_TICK_NS` environment variable.
        /// @return The tick period in nanoseconds, or the built-in default when the variable is unset or malformed.
        [[nodiscard]] long readiness_watchdog_tick_ns_from_env() noexcept;

        /// @brief Enables lsquic's internal debug logging when the corresponding environment variable is set.
        /// @note Called once during engine initialisation; a no-op when debug logging is not requested.
        void maybe_enable_lsquic_debug_logging() noexcept;

        /// @brief Converts an lsquic connection status into a human-readable string for logging.
        /// @param status The lsquic connection status to describe.
        /// @return A static, null-terminated view naming the status; "unknown" for unrecognised values.
        [[nodiscard]] std::string_view conn_status_to_string(const ::LSQUIC_CONN_STATUS status) noexcept;

        /// @brief Translates the portable engine settings into lsquic's native settings structure.
        /// @param lsquic_settings The lsquic settings structure to populate.
        /// @param config          The portable QUIC settings to apply.
        /// @param lsquic_flags    The lsquic engine flags (`LSENG_SERVER`, `LSENG_HTTP`, ...) the settings are validated against.
        void apply_lsquic_settings(::lsquic_engine_settings& lsquic_settings, const kmx::aio::quic::settings& config,
                                   const unsigned lsquic_flags) noexcept;

        /// @brief Tells whether a stream was initiated locally rather than by the peer.
        /// @param stream    The stream to classify.
        /// @param is_client `true` when this endpoint is the client, `false` for a server.
        /// @return `true` if the stream id encodes a locally initiated stream.
        [[nodiscard]] bool is_local_initiated_stream(const ::lsquic_stream_t* stream, const bool is_client) noexcept;

        /// @brief Writes a batch of outgoing lsquic packets to a UDP socket.
        /// @param fd    The bound UDP socket descriptor.
        /// @param specs The array of packet specifications lsquic wants sent.
        /// @param count The number of entries in @p specs.
        /// @return The number of packets actually sent; `-1` on failure with `errno` set.
        [[nodiscard]] int send_packets_out_fd(const int fd, const ::lsquic_out_spec* specs, unsigned count) noexcept;
    }

    /// @brief Where a server engine binds, and how its connections are set up.
    /// @details What @ref generic_engine::start hands to the shared server setup.
    struct start_params
    {
        /// @brief The local IP address to bind to.
        ip_address_t ip;
        /// @brief The local UDP port to bind to.
        port_t port {};
        /// @brief The borrowed OpenSSL `SSL_CTX` to use for the handshake.
        void* ssl_ctx {};
        /// @brief The QUIC settings to apply to the engine.
        kmx::aio::quic::settings config {};
    };
}
