/// @file inc/kmx/aio/knx/secure/tunnel_transport.hpp
/// @brief A stream transport that carries a KNX IP Secure tunnelling session: the handshake on open, every frame wrapped.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// The tunnelling client runs its tunnel over this as over any stream transport, and the session never shows through
/// to it. Opening opens the connection underneath and runs the handshake. Every frame sent is sealed under the session
/// key, and every wrapper received is authenticated, checked and opened before the client sees what it carried. A
/// SESSION_STATUS is the session's own and is applied here. An unwrapped frame is refused with
/// @ref kmx::aio::knx::error::secure_frame_required - except discovery, which KNX IP Secure leaves in the clear.
///
/// A handshake that fails closes the connection, so no plain frame can follow on it (P1). Sends are ordered by an
/// `async_mutex` held across the send, which puts wrappers on the connection in the order of their sequence numbers;
/// the session sits behind a mutex held for its synchronous steps alone, since a receive and the senders reach it from
/// tasks that may run on different threads.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/async_mutex.hpp>
        #include <kmx/aio/knx/datagram_transport.hpp>
        #include <kmx/aio/knx/frame.hpp>
        #include <kmx/aio/knx/secure/client_session.hpp>
        #include <kmx/aio/knx/transport.hpp>

        #include <array>
        #include <cstddef>
        #include <cstdint>
        #include <expected>
        #include <mutex>
        #include <optional>
        #include <type_traits>
        #include <utility>
    #endif

namespace kmx::aio::knx::secure
{
    /// @brief How long the handshake waits for each answer - SESSION_RESPONSE, then SESSION_STATUS - in milliseconds.
    inline constexpr std::uint32_t handshake_timeout_ms = 10'000u;

    /// @brief A KNX IP Secure tunnelling session over a stream transport, presented as a transport itself.
    class tunnel_transport final: public datagram_transport
    {
    public:
        /// @brief Creates the transport; nothing is sent until @ref open.
        /// @param connection The stream transport underneath; must outlive this object.
        /// @param credentials What sessions are opened with.
        /// @param clock_ms The monotonic clock the session's keep-alive and timeout run on; the steady clock when null.
        /// @param entropy Where key pairs come from; must outlive this object.
        tunnel_transport(datagram_transport& connection, tunnelling_credentials credentials, monotonic_ms_function clock_ms,
                         entropy_source& entropy) noexcept;

        /// @brief Opens the connection and runs the handshake, unless a session is established already.
        /// @return A task yielding nothing once the session is established, or why it could not be.
        /// @retval kmx::aio::knx::error::secure_authentication_failed SESSION_RESPONSE did not verify.
        /// @retval kmx::aio::knx::error::secure_session_rejected The server refused the session or the user.
        /// @retval kmx::aio::knx::error::timeout An answer did not come within @ref handshake_timeout_ms.
        /// @note On any failure the connection is closed again.
        [[nodiscard]] task_returning_expected_void_t open() noexcept(false) override;

        /// @brief Ends the session locally, wiping its keys, and closes the connection; nothing is sent.
        void close() noexcept override;

        /// @brief Returns whether the connection underneath is a stream.
        [[nodiscard]] bool stream_oriented() const noexcept override { return connection_.stream_oriented(); }

        /// @brief Seals one frame and sends the wrapper; the address arguments are ignored.
        /// @return A task yielding the plain frame's length, or why it was not sent.
        [[nodiscard]] task_returning_expected_size_t send(cspan_byte_t payload, const sockaddr* peer,
                                                          ::socklen_t peer_length) noexcept(false) override;

        /// @brief Waits for the next frame for the tunnel: an opened wrapper's content, or discovery sent in the clear.
        /// @return A task yielding the frame's length.
        /// @retval kmx::aio::knx::error::secure_frame_required Tunnel traffic arrived unwrapped; it is counted and dropped.
        /// @retval kmx::aio::knx::error::secure_session_closed The server ended the session; the connection is closed.
        /// @note A wrapper that does not authenticate, replays an older sequence number or carries a service that may not
        ///       be wrapped is counted and read past.
        [[nodiscard]] task_returning_expected_size_t receive(span_byte_t buffer, transport_peer& peer) noexcept(false) override;

        /// @brief As @ref receive, giving up at a deadline on the connection's clock.
        [[nodiscard]] task_returning_expected_size_t receive_until(span_byte_t buffer, transport_peer& peer,
                                                                   std::uint32_t deadline_ms) noexcept(false) override;

        /// @brief Sends a SESSION_STATUS keep-alive; send-only, so it may run while a receive waits.
        /// @return A task yielding nothing, or why it was not sent.
        [[nodiscard]] task_returning_expected_void_t keep_alive() noexcept(false);

        /// @brief Sends a SESSION_STATUS close, if a session is open, and ends the session.
        /// @return A task yielding nothing, or why the close could not be sent.
        [[nodiscard]] task_returning_expected_void_t end_session() noexcept(false);

        /// @brief Indicates whether a keep-alive is due.
        [[nodiscard]] bool keep_alive_due() const noexcept;

        /// @brief Ends a session that has carried no traffic for @ref session_timeout_ms, closing the connection.
        /// @return Nothing while the session is alive, or @ref kmx::aio::knx::error::secure_session_closed.
        [[nodiscard]] expected_void_t check_timeout() noexcept;

        /// @brief Indicates whether the session is established.
        [[nodiscard]] bool established() const noexcept;

        /// @brief Returns a copy of what the session has refused and done.
        [[nodiscard]] statistics counters() const noexcept;

    private:
        /// @brief A deadline on the connection's clock, or none.
        using optional_deadline_t = std::optional<std::uint32_t>;
        /// @brief A frame for the tunnel's length; nothing when the frame was the session's own or was read past.
        using unwrapped_t = std::expected<std::optional<std::size_t>, std::error_code>;

        /// @brief Runs one synchronous step on the session, holding its lock.
        template <typename Step>
        [[nodiscard]] std::invoke_result_t<Step, client_session&> with_session(Step&& step) const noexcept
        {
            const std::lock_guard lock {session_mutex_};
            return std::forward<Step>(step)(session_);
        }

        [[nodiscard]] std::uint64_t now_ms() const noexcept;
        [[nodiscard]] task_returning_expected_void_t handshake() noexcept(false);
        [[nodiscard]] task_returning_expected_void_t await_response(std::uint32_t deadline_ms) noexcept(false);
        [[nodiscard]] task_returning_expected_void_t authenticate(const session_response_frame& response) noexcept(false);
        [[nodiscard]] task_returning_expected_void_t await_status(std::uint32_t deadline_ms) noexcept(false);
        [[nodiscard]] task_returning_expected_size_t receive_frame(span_byte_t buffer, transport_peer& peer,
                                                                   optional_deadline_t deadline_ms) noexcept(false);
        /// @brief Receives one datagram from the connection into the wire buffer.
        [[nodiscard]] task_returning_expected_size_t receive_wire(transport_peer& peer, optional_deadline_t deadline_ms) noexcept(false);
        /// @brief Classifies a received datagram and opens it when it is a wrapper.
        [[nodiscard]] unwrapped_t unwrap(std::size_t size, span_uint8_t plain) noexcept;
        /// @brief Sends octets as they are, taking the send lock.
        [[nodiscard]] task_returning_expected_void_t send_wire(cspan_uint8_t wire) noexcept(false);
        /// @brief Sends octets as they are; the caller holds the send lock.
        [[nodiscard]] task_returning_expected_void_t send_locked(cspan_uint8_t wire) noexcept(false);

        datagram_transport& connection_;
        monotonic_ms_function clock_ms_ {};
        /// @brief Guards @ref session_; see @ref with_session.
        mutable std::mutex session_mutex_ {};
        mutable client_session session_;
        /// @brief Orders the sends, so wrappers reach the connection in sequence order.
        async_mutex send_mutex_ {};
        /// @brief What the receive in progress read from the connection.
        std::array<std::uint8_t, frame::max_datagram_size> wire_ {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
