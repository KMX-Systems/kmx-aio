/// @file aio/http2/frame.hpp
/// @brief HTTP/2 frame definitions and utilities.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

/// @brief HTTP/2 core protocol definitions and utilities
namespace kmx::aio::http2
{

    /// @brief Defines standard HTTP/2 frame type identifiers
    enum class frame_type : std::uint8_t
    {
        data = 0u,
        headers = 1u,
        priority = 2u,
        rst_stream = 3u,
        settings = 4u,
        push_promise = 5u,
        ping = 6u,
        goaway = 7u,
        window_update = 8u,
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
    std::size_t make_goaway(std::span<std::uint8_t> buffer, const std::uint32_t last_stream_id,
                            const std::uint32_t error_code) noexcept(false);

} // namespace kmx::aio::http2
