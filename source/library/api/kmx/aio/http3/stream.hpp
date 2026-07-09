/// @file aio/http3/stream.hpp
/// @brief HTTP/3 stream definitions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <kmx/aio/http3/frame.hpp>

#include <cstdint>

namespace kmx::aio::http3
{
    /// @brief HTTP/3 stream state values.
    enum class stream_state : std::uint8_t
    {
        /// @brief Stream has not exchanged frames yet.
        idle = 0u,
        /// @brief Stream is open in both directions.
        open,
        /// @brief Local side is closed for sending.
        half_closed_local,
        /// @brief Remote side is closed for receiving.
        half_closed_remote,
        /// @brief Stream is fully closed.
        closed
    };

    /// @brief Minimal HTTP/3 request/response stream state machine on top of
    /// QUIC stream lifecycle.
    class stream
    {
    public:
        /// @brief Creates a stream state tracker for the given stream ID.
        /// @param id The QUIC stream identifier.
        explicit stream(std::uint64_t id) noexcept: id_ {id} {}

        /// @brief Returns the tracked stream ID.
        /// @return The stream identifier.
        [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
        /// @brief Returns the current stream state.
        /// @return The current state.
        [[nodiscard]] stream_state state() const noexcept { return state_; }

        /// @brief Updates state after sending a frame.
        /// @param type The frame type that was sent.
        void on_frame_sent(frame_type type) noexcept(false);
        /// @brief Updates state after receiving a frame.
        /// @param type The frame type that was received.
        void on_frame_received(frame_type type) noexcept(false);
        /// @brief Marks the local send side as finished.
        void on_send_fin() noexcept;
        /// @brief Marks the remote receive side as finished.
        void on_recv_fin() noexcept;
        /// @brief Resets the stream state to closed.
        void on_reset() noexcept;

    private:
        /// @brief The tracked stream ID.
        std::uint64_t id_ {};
        /// @brief The current stream state.
        stream_state state_ {stream_state::idle};
        /// @brief True once the first HEADERS frame has been observed.
        bool headers_seen_ {false};
        /// @brief True once the local send side is closed.
        bool send_closed_ {false};
        /// @brief True once the remote receive side is closed.
        bool recv_closed_ {false};
    };
} // namespace kmx::aio::http3