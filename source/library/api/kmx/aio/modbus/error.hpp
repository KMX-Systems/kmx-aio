/// @file aio/modbus/error.hpp
/// @brief Modbus-specific error domain for framing and transport failures.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <system_error>
#endif

namespace kmx::aio::modbus
{
    /// @brief Modbus wrapper-level error codes.
    enum class error : std::uint32_t
    {
        /// @brief No error.
        success = 0u,
        /// @brief Modbus feature is disabled at build time.
        feature_disabled,
        /// @brief Provided configuration is invalid or incomplete.
        invalid_configuration,
        /// @brief TCP or TLS connection could not be established.
        connection_failed,
        /// @brief Peer closed the connection or the stream reached EOF.
        disconnected,
        /// @brief Server returned an exception PDU (exception_code in the payload).
        exception_response,
        /// @brief Response function code does not match the request.
        unexpected_function_code,
        /// @brief Response transaction identifier does not match the request.
        unexpected_transaction_id,
        /// @brief Request PDU exceeds the 253-byte protocol limit.
        frame_too_large,
        /// @brief Received frame is malformed or truncated.
        malformed_frame,
        /// @brief Unit identifier in response does not match the request.
        invalid_unit_id,
        /// @brief TLS handshake failed.
        tls_handshake_failed,
        /// @brief Operation exceeded the configured timeout.
        timed_out,
        /// @brief Internal implementation failure.
        internal_error,
    };

    /// @brief Access the Modbus error category singleton.
    /// @return Error category used for @ref error values.
    [[nodiscard]] const std::error_category& error_category() noexcept;

    /// @brief Convert a Modbus error enum value to @c std::error_code.
    /// @param code Modbus error enum value.
    /// @return @c std::error_code bound to @ref error_category.
    [[nodiscard]] std::error_code make_error_code(error code) noexcept;

} // namespace kmx::aio::modbus

namespace std
{
    template <>
    struct is_error_code_enum<kmx::aio::modbus::error>: true_type
    {
    };
}
