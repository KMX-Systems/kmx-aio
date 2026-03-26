#pragma once

#include "frame.hpp"
#include <cstdint>
#include <stdexcept>

/// @brief HTTP/2 core protocol definitions and utilities
namespace kmx::aio::http2
{

    /// @brief Defines the exact HTTP/2 stream states per RFC 7540
    enum class stream_state : std::uint8_t
    {
        idle = 0,
        reserved_local,
        reserved_remote,
        open,
        half_closed_local,
        half_closed_remote,
        closed
    };

    /// @brief Manages the state machine transitions for a single HTTP/2 stream map
    class stream
    {
    private:
        std::uint32_t id_;
        stream_state state_;

    public:
        /// @brief Initializes a new HTTP/2 state machine structure
        /// @param id The remote or local stream identifier
        explicit stream(std::uint32_t id) noexcept: id_ {id}, state_ {stream_state::idle} {}

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
