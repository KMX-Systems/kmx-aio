/// @file aio/http2/stream.hpp
/// @brief HTTP/2 stream state machine definitions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>

    #include <kmx/aio/http2/frame.hpp>
#endif

/// @brief HTTP/2 core protocol definitions and utilities
namespace kmx::aio::http2
{

    /// @brief Defines the exact HTTP/2 stream states per RFC 7540
    enum class stream_state : std::uint8_t
    {
        /// @brief The stream has not been used yet.
        idle = 0,
        /// @brief Reserved by a PUSH_PROMISE this endpoint sent.
        reserved_local,
        /// @brief Reserved by a PUSH_PROMISE the peer sent.
        reserved_remote,
        /// @brief Both endpoints may send frames.
        open,
        /// @brief This endpoint has finished sending; it may still receive.
        half_closed_local,
        /// @brief The peer has finished sending; this endpoint may still send.
        half_closed_remote,
        /// @brief The stream is finished in both directions.
        closed
    };

    /// @brief Manages the state machine transitions for a single HTTP/2 stream map
    class stream
    {
    private:
        /// @brief The stream identifier this state machine tracks.
        std::uint32_t id_;
        /// @brief The current state of the stream.
        stream_state state_;

    public:
        /// @brief Initializes a new HTTP/2 state machine structure
        /// @param id The remote or local stream identifier
        explicit stream(const std::uint32_t id) noexcept: id_ {id}, state_ {stream_state::idle} {}

        /// @brief Gets the exact stream identifier
        [[nodiscard]] std::uint32_t id() const noexcept { return id_; }

        /// @brief Gets the current internal protocol stream state
        [[nodiscard]] stream_state state() const noexcept { return state_; }

        /// @brief Processes an outgoing frame sent *from* this stream and steps the state machine
        /// @param type HTTP/2 Frame type sent
        /// @param end_stream Set true if the END_STREAM flag bit is appended
        void on_frame_sent(frame_type type, bool end_stream) noexcept(false);

        /// @brief Processes an incoming frame received *by* this stream and adjusts internal bounds
        /// @param type HTTP/2 Frame type received over the network layer
        /// @param end_stream Set true if the END_STREAM flag bit was parsed
        void on_frame_received(frame_type type, bool end_stream) noexcept(false);
    };

} // namespace kmx::aio::http2
