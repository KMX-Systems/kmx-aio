/// @file inc/kmx/aio/knx/secure/session_link.hpp
/// @brief One KNX IP Secure session of a tunnelling server, presented as the transport its tunnel answers on.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// A secure server receives on the connection and opens every wrapper in its connection loop, then hands what a wrapper
/// carried to the tunnelling code as though it had arrived on this transport. Whatever that code answers is sent here,
/// and sealed under the session's key on its way to the connection. An `async_mutex` is held across the seal and the send,
/// so the wrappers of one session reach the connection in the order of their sequence numbers.
///
/// A link only sends: the connection loop does the receiving, and @ref kmx::aio::knx::secure::session_link::receive
/// refuses. Once the session has ended every send reports @ref kmx::aio::knx::error::secure_session_closed.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/async_mutex.hpp>
        #include <kmx/aio/knx/datagram_transport.hpp>
        #include <kmx/aio/knx/secure/server_session_table.hpp>
        #include <kmx/aio/knx/secure/session.hpp>
        #include <kmx/aio/knx/transport.hpp>

        #include <cstdint>
    #endif

namespace kmx::aio::knx::secure
{
    /// @brief The sending side of one server session, over the connection the session was opened on.
    class session_link final: public datagram_transport
    {
    public:
        /// @brief Creates the link.
        /// @param connection The connection the session was opened on; must outlive this object.
        /// @param sessions The table holding the session; must outlive this object.
        /// @param session_id The session.
        session_link(datagram_transport& connection, server_session_table& sessions, std::uint16_t session_id) noexcept;

        /// @brief Seals one frame under the session and sends the wrapper on the connection; the address arguments are ignored.
        /// @return A task yielding the frame's length, or why it was not sent.
        /// @retval kmx::aio::knx::error::secure_session_closed The session has ended, or is not authenticated.
        [[nodiscard]] task_returning_expected_size_t send(cspan_byte_t payload, const sockaddr* peer,
                                                          ::socklen_t peer_length) noexcept(false) override;

        /// @brief Refuses: a link only sends.
        /// @return A task yielding @ref kmx::aio::knx::error::unsupported_service.
        [[nodiscard]] task_returning_expected_size_t receive(span_byte_t buffer, transport_peer& peer) noexcept(false) override;

        /// @brief Returns whether the connection underneath is a stream.
        [[nodiscard]] bool stream_oriented() const noexcept override { return connection_.stream_oriented(); }

        /// @brief Seals and sends a SESSION_STATUS.
        /// @return A task yielding nothing, or why it was not sent.
        [[nodiscard]] task_returning_expected_void_t send_status(session_status status) noexcept(false);

        /// @brief Returns the session this link sends for.
        [[nodiscard]] std::uint16_t session_id() const noexcept { return session_id_; }

    private:
        /// @brief Sends sealed octets on the connection; the caller holds the send lock.
        [[nodiscard]] task_returning_expected_void_t send_wire(cspan_uint8_t wire) noexcept(false);

        datagram_transport& connection_;
        server_session_table& sessions_;
        std::uint16_t session_id_ {};
        /// @brief Orders the sends, so wrappers reach the connection in sequence order.
        async_mutex send_mutex_ {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
