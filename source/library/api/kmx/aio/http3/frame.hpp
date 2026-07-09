/// @file aio/http3/frame.hpp
/// @brief HTTP/3 frame definitions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

/// @brief HTTP/3 core protocol definitions and utilities.
/// @details HTTP/3 rides over QUIC and encodes frame and settings identifiers
/// as QUIC varints.
namespace kmx::aio::http3
{
    /// @brief Standard HTTP/3 frame type identifiers from RFC 9114.
    enum class frame_type : std::uint64_t
    {
        /// @brief DATA frame.
        data = 0x0u,
        /// @brief HEADERS frame.
        headers = 0x1u,
        /// @brief CANCEL_PUSH frame.
        cancel_push = 0x3u,
        /// @brief SETTINGS frame.
        settings = 0x4u,
        /// @brief PUSH_PROMISE frame.
        push_promise = 0x5u,
        /// @brief GOAWAY frame.
        goaway = 0x7u,
        /// @brief MAX_PUSH_ID frame.
        max_push_id = 0xDu
    };

    /// @brief Standard HTTP/3 unidirectional stream type identifiers.
    enum class stream_type : std::uint64_t
    {
        /// @brief Control stream.
        control = 0x0u,
        /// @brief Push stream.
        push = 0x1u,
        /// @brief QPACK encoder stream.
        qpack_encoder = 0x2u,
        /// @brief QPACK decoder stream.
        qpack_decoder = 0x3u
    };

    /// @brief Standard HTTP/3 settings identifiers.
    enum class settings_identifier : std::uint64_t
    {
        /// @brief QPACK maximum table capacity setting.
        qpack_max_table_capacity = 0x1u,
        /// @brief Maximum field section size setting.
        max_field_section_size = 0x6u,
        /// @brief QPACK blocked streams setting.
        qpack_blocked_streams = 0x7u,
        /// @brief ENABLE_CONNECT_PROTOCOL setting.
        enable_connect_protocol = 0x8u,
        /// @brief H3_DATAGRAM setting.
        h3_datagram = 0x33u
    };

    /// @brief A decoded HTTP/3 settings entry.
    struct settings_entry
    {
        /// @brief The setting identifier.
        settings_identifier identifier {};
        /// @brief The associated setting value.
        std::uint64_t value {};
    };

    /// @brief A typed HTTP/3 GOAWAY frame payload.
    struct goaway_frame
    {
        /// @brief The lowest stream ID that may still be processed.
        std::uint64_t stream_id {};
    };

    /// @brief A decoded HTTP/3 frame with payload ownership.
    struct frame
    {
        /// @brief The decoded frame type.
        frame_type type {};
        /// @brief The frame payload bytes.
        std::vector<std::uint8_t> payload {};
    };

    /// @brief Standard HTTP/3 application error codes.
    enum class error_code : std::uint64_t
    {
        /// @brief No error.
        no_error = 0x100u,
        /// @brief General protocol error.
        general_protocol_error = 0x101u,
        /// @brief Internal implementation error.
        internal_error = 0x102u,
        /// @brief Stream creation failed.
        stream_creation_error = 0x103u,
        /// @brief Critical stream closed unexpectedly.
        closed_critical_stream = 0x104u,
        /// @brief Frame type is unexpected for the current context.
        frame_unexpected = 0x105u,
        /// @brief Frame payload or envelope is malformed.
        frame_error = 0x106u,
        /// @brief Peer or local endpoint is overloaded.
        excessive_load = 0x107u,
        /// @brief Stream or identifier value is invalid.
        id_error = 0x108u,
        /// @brief SETTINGS payload is invalid.
        settings_error = 0x109u,
        /// @brief Mandatory SETTINGS frame is missing.
        missing_settings = 0x10Au,
        /// @brief Request was rejected.
        request_rejected = 0x10Bu,
        /// @brief Request was cancelled.
        request_cancelled = 0x10Cu,
        /// @brief Request ended before completion.
        request_incomplete = 0x10Du,
        /// @brief Demo message payload is malformed.
        message_error = 0x10Eu,
        /// @brief CONNECT request failed.
        connect_error = 0x10Fu,
        /// @brief Version fallback is required.
        version_fallback = 0x110u
    };

    class http3_error_category final: public std::error_category
    {
    public:
        /// @brief Returns the stable error-category name.
        [[nodiscard]] const char* name() const noexcept override;

        /// @brief Returns the human-readable message for an HTTP/3 error code.
        /// @param ev The encoded error value.
        /// @return A descriptive error string.
        [[nodiscard]] std::string message(int ev) const override;
    };

    /// @brief Returns the singleton HTTP/3 error category instance.
    /// @return The HTTP/3 error category.
    [[nodiscard]] const std::error_category& http3_error_category_instance() noexcept;

    /// @brief Builds a `std::error_code` from an HTTP/3 protocol error value.
    /// @param code The HTTP/3 protocol error.
    /// @return The corresponding error code object.
    [[nodiscard]] std::error_code make_error_code(error_code code) noexcept;
} // namespace kmx::aio::http3

namespace std
{
    template <>
    struct is_error_code_enum<kmx::aio::http3::error_code>: true_type
    {
    };
} // namespace std