/// @file aio/knx/error.hpp
/// @brief KNX wrapper-level error codes.
/// @details
/// The pure protocol layer — addresses, cEMI, datapoint types and the KNXnet/IP codec — reports failures
/// as this enumeration rather than as `std::error_code`, because `make_error_code` reaches a
/// function-local static category and is therefore not a constant expression. Returning the enumeration
/// is what keeps the codec `constexpr` and its golden vectors checkable with `static_assert`.
/// The I/O layer converts once, at its boundary, with @ref kmx::aio::knx::make_error_code.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <cstdint>
        #include <system_error>
    #endif

namespace kmx::aio::knx
{
    /// @brief Failure reasons reported by the KNX protocol and session layers.
    enum class error : std::uint32_t
    {
        /// @brief No failure.
        success = 0u,
        /// @brief The KNX feature was not compiled into this build.
        feature_disabled,
        /// @brief An operation was requested that the current configuration or state does not allow.
        invalid_configuration,
        /// @brief A frame is truncated, or a field holds a value the encoding does not allow.
        malformed_frame,
        /// @brief A KNXnet/IP service type is well-formed but not implemented.
        unsupported_service,
        /// @brief A host protocol address information block names a protocol this build does not carry.
        unsupported_hpai,
        /// @brief A connect request asks for a connection type this build does not carry.
        unsupported_connection_type,
        /// @brief A declared length disagrees with the buffer it describes.
        invalid_length,
        /// @brief A peer refused the connection, or answered with a non-zero status.
        connection_failed,
        /// @brief A response did not arrive within its deadline.
        timeout,
        /// @brief Consecutive connection-state failures reached the configured limit.
        heartbeat_failed,
        /// @brief No traffic was seen for the configured inactivity period.
        inactivity_timeout,
        /// @brief A channel identifier or sequence number does not match the session.
        sequence_error,
        /// @brief The outgoing queue is at its configured limit.
        send_queue_full,
        /// @brief The session is shutting down or already closed.
        shutdown,
        /// @brief An invariant of the implementation was violated.
        internal_error,
        /// @brief An address component does not fit its field, or address text is malformed.
        invalid_address,
        /// @brief A cEMI message code is not one this build decodes.
        unsupported_message_code,
        /// @brief An application layer service (APCI) is not one this build decodes.
        unsupported_apci,
        /// @brief An application payload exceeds what the frame format can carry.
        payload_too_large,
        /// @brief A datapoint type is not implemented, or does not match the payload width.
        unsupported_datapoint,
        /// @brief A datapoint value lies outside the range its type can represent.
        value_out_of_range,
        /// @brief A requested KNX Secure profile or cryptographic operation is unavailable.
        secure_unsupported,
        /// @brief A message authentication code did not verify.
        /// @note Every verification failure reports this one value, whichever part of the frame was wrong.
        secure_authentication_failed,
        /// @brief An authenticated sequence number or timer value is outside the acceptance rule, or repeats
        ///        one already accepted.
        secure_replay,
        /// @brief The peer refused to authenticate a secure session.
        secure_session_rejected,
        /// @brief The secure session was closed by the peer, or timed out.
        secure_session_closed,
        /// @brief No key is configured for the group address, sender or user a frame names, or the sender
        ///        is not allowed to send to that group.
        secure_key_missing,
        /// @brief An unencrypted frame arrived for a service the connection requires to be secured.
        secure_frame_required,
        /// @brief A keyring's signature does not verify: the password is wrong, or the document was altered.
        keyring_signature_invalid,
        /// @brief The cryptographic backend reported a failure.
        crypto_failure,
    };

    /// @brief Returns the error category that names KNX errors.
    [[nodiscard]] const std::error_category& error_category() noexcept;

    /// @brief Converts a KNX error into an `std::error_code`.
    /// @param code The protocol-layer error to convert.
    /// @return The equivalent `std::error_code` in the KNX category.
    /// @note This is the single conversion point between the pure layer and the I/O layer.
    [[nodiscard]] std::error_code make_error_code(error code) noexcept;
}

namespace std
{
    /// @brief Marks @ref kmx::aio::knx::error as convertible to `std::error_code`.
    template <>
    struct is_error_code_enum<kmx::aio::knx::error>: true_type
    {
    };
}
#endif // KMX_AIO_FEATURE_KNX
