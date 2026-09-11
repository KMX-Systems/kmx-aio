/// @file aio/completion/knx/tcp_transport.hpp
/// @brief Completion TCP adapter for the KNX transport contract: one KNXnet/IP connection over TCP.
/// @details
/// The io_uring twin of the readiness TCP transport, and the same contract: frames are recovered whole from the
/// stream, sends never interleave, a send's address is ignored, and a receive reports the connection's peer. The
/// kernel performs each read and write itself, so there is no readiness edge to lose. As there, a receive that fails
/// for any reason but its deadline closes the connection, since a stream that lost its place cannot be brought back
/// into step.
/// @note @ref kmx::aio::completion::knx::tcp_transport::open waits as long as the kernel's own connect does; a
///       deadline on the connect is the readiness transport's alone for now.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_COMPLETION) && defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <cstdint>
        #include <optional>
        #include <sys/socket.h>
    #endif

    #include <kmx/aio/async_mutex.hpp>
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/file_descriptor.hpp>
    #include <kmx/aio/knx/detail/frame_reassembler.hpp>
    #include <kmx/aio/knx/transport.hpp>

namespace kmx::aio::completion::knx
{
    /// @brief One KNXnet/IP connection over TCP, presented as a KNX transport.
    /// @note One receive may be outstanding at a time; sends may overlap it and each other.
    class tcp_transport final: public kmx::aio::knx::datagram_transport
    {
    public:
        /// @brief Creates a client-side transport, which connects to @p peer when opened.
        /// @param exec The completion executor that performs the I/O.
        /// @param peer The server's address; only @p peer_length octets are read.
        /// @param peer_length The number of octets @p peer provides.
        tcp_transport(executor& exec, const sockaddr* peer, ::socklen_t peer_length) noexcept;

        /// @brief Creates a server-side transport over a connection already accepted.
        /// @param exec The completion executor that performs the I/O.
        /// @param connection The accepted connection.
        /// @param peer The client's address.
        /// @param peer_length The number of octets of @p peer that are meaningful.
        tcp_transport(executor& exec, file_descriptor&& connection, const sockaddr_storage& peer, ::socklen_t peer_length) noexcept;

        tcp_transport(const tcp_transport&) = delete;
        tcp_transport& operator=(const tcp_transport&) = delete;
        /// @brief Closes the connection.
        ~tcp_transport() noexcept override;

        /// @brief Connects, unless the transport is connected already.
        /// @return A task yielding nothing, or @ref kmx::aio::knx::error::connection_failed.
        [[nodiscard]] task_returning_expected_void_t open() noexcept(false) override;

        /// @brief Closes the connection; a read or write still pending completes, and anything half-received is dropped.
        void close() noexcept override;

        /// @brief Always true: frames travel on a byte stream.
        [[nodiscard]] bool stream_oriented() const noexcept override { return true; }

        /// @brief Indicates whether the connection is open.
        [[nodiscard]] bool is_open() const noexcept { return connection_.is_valid(); }

        /// @brief Sends one frame whole; the address arguments are ignored, since the connection has one peer.
        [[nodiscard]] task_returning_expected_size_t send(cspan_byte_t payload, const sockaddr* peer,
                                                          ::socklen_t peer_length) noexcept(false) override;

        /// @brief Waits for the next whole frame.
        /// @retval kmx::aio::knx::error::shutdown The peer closed the connection between frames.
        /// @retval kmx::aio::knx::error::malformed_frame The peer closed it in the middle of a frame, or a header is wrong.
        [[nodiscard]] task_returning_expected_size_t receive(span_byte_t buffer,
                                                             kmx::aio::knx::transport_peer& peer) noexcept(false) override;

        /// @brief Waits for the next whole frame, giving up at a deadline without losing what arrived of it.
        [[nodiscard]] task_returning_expected_size_t receive_until(span_byte_t buffer, kmx::aio::knx::transport_peer& peer,
                                                                   std::uint32_t deadline_ms) noexcept(false) override;

    private:
        /// @brief A deadline, or none.
        using optional_deadline_t = std::optional<std::uint32_t>;

        [[nodiscard]] task_returning_expected_size_t receive_frame(span_byte_t buffer, kmx::aio::knx::transport_peer& peer,
                                                                   optional_deadline_t deadline_ms) noexcept(false);
        /// @brief Reads what arrives next into the reassembler.
        [[nodiscard]] task_returning_expected_void_t fill(optional_deadline_t deadline_ms) noexcept(false);
        /// @brief Reports a failed receive, closing the connection unless the failure was a deadline.
        [[nodiscard]] std::unexpected<std::error_code> end_stream(std::error_code failure) noexcept;
        /// @brief Copies a frame out to the caller and names the peer.
        [[nodiscard]] expected_size_t deliver(cspan_uint8_t frame, span_byte_t buffer, kmx::aio::knx::transport_peer& peer) const noexcept;

        executor& exec_;
        sockaddr_storage peer_ {};
        ::socklen_t peer_length_ {};
        file_descriptor connection_ {};
        kmx::aio::knx::detail::frame_reassembler reassembler_ {};
        async_mutex write_mutex_ {};
    };
}
#endif // KMX_AIO_FEATURE_COMPLETION && KMX_AIO_FEATURE_KNX
