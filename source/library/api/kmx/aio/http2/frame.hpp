/// @file aio/http2/frame.hpp
/// @brief HTTP/2 frame definitions and utilities.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
#endif

/// @brief HTTP/2 core protocol definitions and utilities
namespace kmx::aio::http2
{
    /// @brief Defines standard HTTP/2 frame type identifiers
    enum class frame_type : std::uint8_t
    {
        /// @brief Carries request or response body octets.
        data = 0u,
        /// @brief Opens a stream and carries a header block fragment.
        headers = 1u,
        /// @brief Adjusts the priority of a stream (deprecated by RFC 9113).
        priority = 2u,
        /// @brief Terminates a single stream with an error code.
        rst_stream = 3u,
        /// @brief Carries connection configuration parameters.
        settings = 4u,
        /// @brief Announces a server-pushed stream.
        push_promise = 5u,
        /// @brief Measures round-trip time and checks connection liveness.
        ping = 6u,
        /// @brief Initiates connection shutdown and reports the last processed stream.
        goaway = 7u,
        /// @brief Grants additional flow-control credit.
        window_update = 8u,
        /// @brief Continues a header block begun by HEADERS or PUSH_PROMISE.
        continuation = 9u
    };

#pragma pack(push, 1)
    /// @brief Represents a standard 9-byte HTTP/2 frame header
    struct frame_header
    {
        std::uint8_t length[3u]; ///< 24-bit payload length
        frame_type type;         ///< 8-bit frame type
        std::uint8_t flags;      ///< 8-bit frame flags
        std::uint32_t stream_id; ///< 31-bit stream ID (1-bit reserved)
    };
#pragma pack(pop)

    /// @brief Creates a complete GOAWAY frame for graceful connection teardown
    /// @param buffer Destination buffer
    /// @param last_stream_id The highest stream ID successfully processed
    /// @param error_code The reason for closing the connection
    /// @return Number of bytes written
    /// @throws std::invalid_argument if buffer is too small
    std::size_t make_goaway(span_uint8_t buffer, const std::uint32_t last_stream_id, const std::uint32_t error_code) noexcept(false);

} // namespace kmx::aio::http2
