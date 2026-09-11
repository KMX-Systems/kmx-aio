/// @file kmx/aio/knx/secure/server_session_table.hpp
/// @brief The sessions of a KNX IP Secure tunnelling server, without I/O: allocation, the handshake, limits and timeouts.
/// @details
/// A secure tunnelling server holds one session per client. This table answers SESSION_REQUEST - a fresh key pair and
/// session key per session (P3), and the device authentication MAC - then opens every wrapper a client sends, checks
/// SESSION_AUTHENTICATE against its users, and seals what the server sends back. It never sends or receives: its owner
/// passes in what arrived, sends what comes back, and tells it the monotonic time.
///
/// A received wrapper is checked in the order the client's session checks one: the session and the connection it
/// belongs to, the MAC, a sequence number above the last one accepted, and the service it carries (P2). Users are held as
/// derived keys only (P4). Sessions are bounded in total and, while unauthenticated, per peer address. A session that
/// stays unauthenticated past its lifetime, or from which nothing arrives for @ref kmx::aio::knx::secure::session_timeout_ms,
/// is reaped. What the server sends keeps no session alive, so a client that has gone away is noticed however much the bus
/// has to say to it.
///
/// Every member takes the table's lock for its own duration, so connection loops and application sends may reach it from
/// any thread. The lock is never held across I/O, and nothing outside the table is called under it.
/// @reference KNX System Specifications, 03/08/09 "KNXnet/IP Security".
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <cstddef>
        #include <cstdint>
        #include <expected>
        #include <memory>
        #include <mutex>
        #include <optional>
        #include <span>
        #include <system_error>
        #include <vector>
    #endif

    #include <kmx/aio/knx/address.hpp>
    #include <kmx/aio/knx/secure/client_session.hpp>
    #include <kmx/aio/knx/secure/common.hpp>
    #include <kmx/aio/knx/secure/entropy.hpp>
    #include <kmx/aio/knx/secure/key.hpp>
    #include <kmx/aio/knx/secure/server_configuration.hpp>
    #include <kmx/aio/knx/secure/session.hpp>
    #include <kmx/aio/knx/secure/wrapper.hpp>
    #include <kmx/aio/knx/transport.hpp>

namespace kmx::aio::knx::secure
{
    /// @brief What a received wrapper turned out to be.
    enum class server_frame_kind : std::uint8_t
    {
        /// @brief A frame for the tunnel, from an authenticated session.
        tunnel,
        /// @brief SESSION_AUTHENTICATE that verified: answer SESSION_STATUS authentication success.
        authenticated,
        /// @brief SESSION_AUTHENTICATE that did not verify: answer SESSION_STATUS authentication failed, then close the
        ///        session.
        refused_authentication,
        /// @brief A SESSION_STATUS keep-alive.
        keep_alive,
        /// @brief A SESSION_STATUS ending the session, which is already gone.
        closed,
    };

    /// @brief An opened wrapper: its session, what it held, and how long the frame for the tunnel is.
    struct server_opened_frame
    {
        /// @brief The session the wrapper belongs to.
        std::uint16_t session_id {};
        /// @brief The frame's length; zero unless @ref kind is @ref server_frame_kind::tunnel.
        std::size_t size {};
        /// @brief What the wrapper held.
        server_frame_kind kind {server_frame_kind::tunnel};
    };

    /// @brief An opened wrapper, or why it was refused.
    using server_opened_frame_result_t = std::expected<server_opened_frame, std::error_code>;
    /// @brief A SESSION_RESPONSE to send, or why the request was refused.
    using session_response_result_t = std::expected<session_response_frame, std::error_code>;

    /// @brief The secure sessions of one tunnelling server.
    class server_session_table final
    {
    public:
        /// @brief Creates an empty table, with room for as many sessions as the configuration allows.
        /// @param configuration The device authentication code, users and limits; must not be null.
        /// @param entropy Where key pairs come from; must outlive this object.
        /// @throws std::bad_alloc when the room cannot be allocated.
        server_session_table(std::shared_ptr<const server_configuration> configuration, entropy_source& entropy) noexcept(false);

        server_session_table(const server_session_table&) = delete;
        server_session_table& operator=(const server_session_table&) = delete;

        /// @brief Answers a SESSION_REQUEST: opens a session, draws its key pair and derives its key.
        /// @param connection The connection the request arrived on; the session belongs to it.
        /// @param peer The client's address, for the per-peer limit.
        /// @param request The decoded request.
        /// @param now_ms The monotonic time.
        /// @return The SESSION_RESPONSE to send in the clear.
        /// @retval kmx::aio::knx::error::send_queue_full Every session is taken, or @p peer holds its share of
        ///         unauthenticated ones.
        /// @retval kmx::aio::knx::error::crypto_failure No key pair could be drawn, or the client's key agrees on nothing.
        [[nodiscard]] session_response_result_t on_session_request(const datagram_transport* connection, const transport_peer& peer,
                                                                   const session_request_frame& request, std::uint64_t now_ms) noexcept;

        /// @brief Authenticates and decrypts a wrapper received from @p peer on @p connection, and applies it when it is
        ///        the session's own.
        /// @param connection The connection the wrapper arrived on.
        /// @param peer The sender the wrapper arrived from.
        /// @param wrapper The decoded wrapper.
        /// @param destination Receives the frame it carried.
        /// @param now_ms The monotonic time.
        /// @return What the wrapper held.
        /// @retval kmx::aio::knx::error::secure_authentication_failed No such session on @p connection from @p peer, or a MAC
        ///         that does not verify.
        /// @retval kmx::aio::knx::error::secure_replay A sequence number not above the last one accepted.
        /// @retval kmx::aio::knx::error::unsupported_service A service that may not be wrapped, or anything but
        ///         SESSION_AUTHENTICATE and SESSION_STATUS before the session's user is authenticated.
        [[nodiscard]] server_opened_frame_result_t open(const datagram_transport* connection, const transport_peer& peer,
                                const secure_wrapper_frame& wrapper, span_uint8_t destination,
                                std::uint64_t now_ms) noexcept;

        /// @brief Seals a frame for an authenticated session under its next sequence number.
        /// @param connection The connection the wrapper is for; a session is sealed for its own connection only.
        /// @return The wrapper's length.
        /// @retval kmx::aio::knx::error::secure_session_closed The session is gone, is not authenticated, or belongs to
        ///         another connection.
        [[nodiscard]] expected_size_t seal(const datagram_transport* connection, std::uint16_t session_id, cspan_uint8_t plain_frame,
                                           span_uint8_t destination) noexcept;

        /// @brief Seals a SESSION_STATUS for a session, authenticated or not.
        [[nodiscard]] expected_size_t seal_status(const datagram_transport* connection, std::uint16_t session_id, session_status status,
                                                  span_uint8_t destination) noexcept;

        /// @brief Returns the tunnel addresses the session's user may be given; empty before authentication.
        /// @note The addresses belong to the configuration, which lives as long as the table.
        [[nodiscard]] std::span<const individual_address> tunnel_addresses(std::uint16_t session_id) const noexcept;

        /// @brief Indicates whether a session is open.
        [[nodiscard]] bool alive(std::uint16_t session_id) const noexcept;

        /// @brief Indicates whether a session is open and its user authenticated.
        [[nodiscard]] bool authenticated(std::uint16_t session_id) const noexcept;

        /// @brief Indicates whether any session is open on a connection.
        [[nodiscard]] bool has_sessions(const datagram_transport* connection) const noexcept;

        /// @brief Ends one session.
        void close(std::uint16_t session_id) noexcept;

        /// @brief Ends every session of a connection.
        /// @return How many sessions ended.
        std::size_t close_connection(const datagram_transport* connection) noexcept;

        /// @brief Ends sessions unauthenticated past their lifetime, and sessions nothing has arrived from for
        ///        @ref session_timeout_ms.
        /// @return How many sessions ended.
        std::size_t reap(std::uint64_t now_ms) noexcept;

        /// @brief Counts a frame refused because it arrived unencrypted.
        void note_unencrypted() noexcept;

        /// @brief Returns how many sessions are open.
        [[nodiscard]] std::size_t sessions() const noexcept;

        /// @brief Returns a copy of what the table has refused and done.
        [[nodiscard]] statistics counters() const noexcept;

    private:
        /// @brief One session.
        struct entry
        {
            std::uint16_t session_id {};
            /// @brief The connection the session was opened on; compared, never followed.
            const datagram_transport* connection {};
            transport_peer peer {};
            x25519_public_key_t client_public_key {};
            x25519_public_key_t server_public_key {};
            secret_key session_key {};
            /// @brief The authenticated user, in the configuration; null until SESSION_AUTHENTICATE verified.
            const tunnelling_user* user {};
            std::uint64_t created_ms {};
            std::uint64_t last_received_ms {};
            std::uint64_t next_sequence {};
            std::optional<std::uint64_t> last_received_sequence {};
        };

        // Every member below expects the lock held.
        [[nodiscard]] std::size_t session_limit() const noexcept;
        [[nodiscard]] expected_void_t admit_request(const transport_peer& peer) const noexcept;
        [[nodiscard]] std::uint16_t allocate_session_id() noexcept;
        [[nodiscard]] entry* find(std::uint16_t session_id) noexcept;
        [[nodiscard]] const entry* find(std::uint16_t session_id) const noexcept;
        [[nodiscard]] const tunnelling_user* find_user(std::uint8_t user_id) const noexcept;
        [[nodiscard]] expected_void_t admit(entry& session, const secure_wrapper_frame& wrapper, span_uint8_t plain) noexcept;
        [[nodiscard]] server_opened_frame_result_t apply(entry& session, cspan_uint8_t plain) noexcept;
        [[nodiscard]] server_opened_frame_result_t apply_status(entry& session, session_status status) noexcept;
        [[nodiscard]] server_opened_frame authenticate(entry& session, const session_authenticate_frame& value) noexcept;
        [[nodiscard]] expected_size_t seal_locked(const datagram_transport* connection, std::uint16_t session_id, cspan_uint8_t plain_frame,
                                                  span_uint8_t destination, bool authenticated_only) noexcept;
        void erase(std::uint16_t session_id) noexcept;

        std::shared_ptr<const server_configuration> configuration_;
        entropy_source& entropy_;
        /// @brief Guards everything below.
        mutable std::mutex mutex_ {};
        std::vector<entry> sessions_ {};
        std::uint16_t next_session_id_ = 1u;
        statistics counters_ {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
