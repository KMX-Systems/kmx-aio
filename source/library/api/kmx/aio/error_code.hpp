/// @file aio/error_code.hpp
/// @brief Standardized error codes for all KMX AIO asynchronous operations.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <string_view>
    #include <system_error>
#endif

namespace kmx::aio
{
    /// @brief Application-level error conditions for asynchronous I/O
    /// operations.
    /// @note  Designed for use with std::expected to enable zero-cost error
    /// propagation
    ///        in latency-critical paths, avoiding C++ exception unwinding
    ///        overhead.
    enum class error_code : std::uint32_t
    {
        success = 0,                  ///< Operation completed successfully.
        connection_reset,             ///< Peer reset the connection.
        connection_refused,           ///< Connection was refused by peer.
        connection_aborted,           ///< Connection was aborted locally.
        connection_timeout,           ///< Connection timed out.
        broken_pipe,                  ///< Attempted write on a closed stream.
        end_of_stream,                ///< End of stream reached (graceful close).
        would_block,                  ///< Non-blocking operation would block.
        invalid_argument,             ///< Invalid argument supplied to operation.
        bad_descriptor,               ///< File descriptor is not valid.
        address_in_use,               ///< Address/port already bound.
        address_not_available,        ///< Requested address is not available.
        not_connected,                ///< Socket is not connected.
        operation_cancelled,          ///< Async operation was cancelled (e.g. via
                                      ///< io_uring cancel).
        buffer_overflow,              ///< Supplied buffer is too small for the result.
        tls_handshake_failed,         ///< TLS handshake could not complete.
        tls_certificate_error,        ///< TLS certificate verification failed.
        quic_protocol_error,          ///< QUIC protocol-level error.
        openonload_not_available,     ///< OpenOnload runtime was requested but
                                      ///< unavailable.
        openonload_init_failed,       ///< OpenOnload stack initialization failed.
        xdp_setup_failed,             ///< AF_XDP UMEM or ring setup failed.
        xdp_umem_registration_failed, ///< AF_XDP UMEM registration failed.
        xdp_ring_setup_failed,        ///< AF_XDP ring setup failed.
        xdp_queue_bind_failed,        ///< AF_XDP queue bind failed.
        spdk_env_init_failed,         ///< SPDK environment initialization failed.
        spdk_probe_failed,            ///< SPDK transport/device probe failed.
        spdk_queue_pair_failed,       ///< SPDK queue-pair creation failed.
        spdk_io_submit_failed,        ///< SPDK I/O submission failed.
        spdk_io_completion_failed,    ///< SPDK I/O completion failed.
        ring_full,                    ///< Submission or completion ring is full.
        unsupported_operation,        ///< Operation not supported on this backend.
        internal_error,               ///< Unspecified internal error.
        unknown                       ///< Unknown / unmapped error.
    };

    /// @brief Returns a human-readable string for the given error code.
    /// @param ec The error code to describe.
    /// @return A view into a static string describing the error.
    [[nodiscard]] std::string_view to_string(error_code ec) noexcept;

    /// @brief Maps a POSIX errno value to a kmx::aio::error_code.
    /// @param err The errno value.
    /// @return The corresponding application-level error code.
    [[nodiscard]] error_code from_errno(int err) noexcept;

    /// @brief Maps a kmx::aio::error_code back to a std::error_code for
    /// interop.
    /// @param ec The application-level error code.
    /// @return A std::error_code in std::generic_category().
    [[nodiscard]] std::error_code to_std_error_code(error_code ec) noexcept;

} // namespace kmx::aio
