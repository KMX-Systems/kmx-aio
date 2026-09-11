/// @file aio/readiness/knx/tcp_transport.hpp
/// @brief Readiness TCP adapter for the KNX transport contract: one KNXnet/IP connection over TCP.
/// @details
/// KNXnet/IP over TCP carries the same frames as UDP, one after another on a byte stream, so this adapter does the two
/// things a datagram socket did for free: it recovers whole frames from the stream, and it keeps two senders' frames
/// from interleaving on it. A connection has one peer, so the address a send names is ignored and a receive always
/// reports that peer.
///
/// A client-side transport connects when @ref kmx::aio::readiness::knx::tcp_transport::open is awaited; a server-side
/// one wraps a connection the listener already accepted. Every wait is bounded and followed by a direct look at the
/// socket: descriptors are registered edge-triggered, and an edge that fires before a wait subscribes is lost.
///
/// A receive that fails for any reason but its deadline closes the connection. A byte stream that lost its place - a
/// frame cut short, a header that does not parse - cannot be brought back into step, so every later receive reports
/// @ref kmx::aio::knx::error::shutdown instead of misreading what follows.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_READINESS) && defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <cstdint>
        #include <optional>
        #include <sys/socket.h>
    #endif

    #include <kmx/aio/async_mutex.hpp>
    #include <kmx/aio/file_descriptor.hpp>
    #include <kmx/aio/knx/detail/frame_reassembler.hpp>
    #include <kmx/aio/knx/transport.hpp>
    #include <kmx/aio/readiness/basic_types.hpp>
    #include <kmx/aio/readiness/executor.hpp>

namespace kmx::aio::readiness::knx
{
    /// @brief One KNXnet/IP connection over TCP, presented as a KNX transport.
    /// @note One receive may be outstanding at a time; sends may overlap it and each other.
    class tcp_transport final: public kmx::aio::knx::datagram_transport
    {
    public:
        /// @brief How long @ref open waits for the connection unless told otherwise.
        static constexpr std::uint32_t default_connect_timeout_ms = 10'000u;

        /// @brief Creates a client-side transport, which connects to @p peer when opened.
        /// @param exec The executor the connection is registered with.
        /// @param peer The server's address; only @p peer_length octets are read.
        /// @param peer_length The number of octets @p peer provides.
        /// @param connect_timeout_ms How long @ref open waits for the connection.
        tcp_transport(executor& exec, const sockaddr* peer, ::socklen_t peer_length,
                      std::uint32_t connect_timeout_ms = default_connect_timeout_ms) noexcept;

        /// @brief Creates a server-side transport over a connection the listener accepted and registered.
        /// @param exec The executor @p connection is registered with.
        /// @param connection The accepted, non-blocking connection.
        /// @param peer The client's address.
        /// @param peer_length The number of octets of @p peer that are meaningful.
        tcp_transport(executor& exec, file_descriptor&& connection, const sockaddr_storage& peer, ::socklen_t peer_length) noexcept;

        tcp_transport(const tcp_transport&) = delete;
        tcp_transport& operator=(const tcp_transport&) = delete;
        /// @brief Closes the connection.
        ~tcp_transport() noexcept override;

        /// @brief Connects, unless the transport is connected already.
        /// @return A task yielding nothing, or why the connection was not made.
        /// @retval kmx::aio::knx::error::timeout The connection was not made within the connect timeout.
        /// @retval kmx::aio::knx::error::connection_failed The server refused, or the socket could not be set up.
        [[nodiscard]] task_returning_expected_void_t open() noexcept(false) override;

        /// @brief Closes the connection; a wait on it ends with a cancellation, and anything half-received is dropped.
        void close() noexcept override;

        /// @brief Always true: frames travel on a byte stream.
        [[nodiscard]] bool stream_oriented() const noexcept override { return true; }

        /// @brief Indicates whether the connection is open.
        [[nodiscard]] bool is_open() const noexcept { return connection_.is_valid(); }

        /// @brief Sends one frame whole; the address arguments are ignored, since the connection has one peer.
        /// @param payload The frame.
        /// @return A task yielding the octets sent - all of them - or the error that stopped the send.
        [[nodiscard]] task_returning_expected_size_t send(cspan_byte_t payload, const sockaddr* peer,
                                                          ::socklen_t peer_length) noexcept(false) override;

        /// @brief Waits for the next whole frame.
        /// @param buffer Receives the frame.
        /// @param peer Filled in with the connection's peer.
        /// @return A task yielding the frame's length.
        /// @retval kmx::aio::knx::error::shutdown The peer closed the connection between frames.
        /// @retval kmx::aio::knx::error::malformed_frame The peer closed it in the middle of a frame, or a header is wrong.
        [[nodiscard]] task_returning_expected_size_t receive(span_byte_t buffer,
                                                             kmx::aio::knx::transport_peer& peer) noexcept(false) override;

        /// @brief Waits for the next whole frame, giving up at a deadline without losing what arrived of it.
        /// @param buffer Receives the frame.
        /// @param peer Filled in with the connection's peer.
        /// @param deadline_ms When to give up, as a monotonic millisecond stamp.
        /// @return As for @ref receive, or @ref kmx::aio::knx::error::timeout.
        [[nodiscard]] task_returning_expected_size_t receive_until(span_byte_t buffer, kmx::aio::knx::transport_peer& peer,
                                                                   std::uint32_t deadline_ms) noexcept(false) override;

    private:
        /// @brief A deadline, or none.
        using optional_deadline_t = std::optional<std::uint32_t>;

        [[nodiscard]] task_returning_expected_size_t receive_frame(span_byte_t buffer, kmx::aio::knx::transport_peer& peer,
                                                                   optional_deadline_t deadline_ms) noexcept(false);
        /// @brief Reads what the socket holds into the reassembler, waiting until something arrives.
        [[nodiscard]] task_returning_expected_void_t fill(optional_deadline_t deadline_ms) noexcept(false);
        /// @brief Waits one bounded slice for readiness, or reports the deadline as passed.
        [[nodiscard]] task_returning_expected_void_t await_io(event_type type, optional_deadline_t deadline_ms) noexcept(false);
        /// @brief Reports a failed receive, closing the connection unless the failure was a deadline.
        [[nodiscard]] std::unexpected<std::error_code> end_stream(std::error_code failure) noexcept;
        /// @brief Copies a frame out to the caller and names the peer.
        [[nodiscard]] expected_size_t deliver(cspan_uint8_t frame, span_byte_t buffer, kmx::aio::knx::transport_peer& peer) const noexcept;

        executor& exec_;
        sockaddr_storage peer_ {};
        ::socklen_t peer_length_ {};
        std::uint32_t connect_timeout_ms_ = default_connect_timeout_ms;
        file_descriptor connection_ {};
        kmx::aio::knx::detail::frame_reassembler reassembler_ {};
        async_mutex write_mutex_ {};
    };
}
#endif // KMX_AIO_FEATURE_READINESS && KMX_AIO_FEATURE_KNX
