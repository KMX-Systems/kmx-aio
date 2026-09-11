/// @file inc/kmx/aio/knx/secure/client_session.hpp
/// @brief The client side of a KNX IP Secure tunnelling session, without I/O.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// Opens the session - SESSION_REQUEST, a verified SESSION_RESPONSE, SESSION_AUTHENTICATE under the new key, and the
/// server's SESSION_STATUS - then seals every frame the client sends and opens every wrapper it receives. It never sends
/// or receives: each call either encodes octets for its owner to send or takes octets its owner received, and each is
/// told the monotonic time.
///
/// Every received wrapper is checked in one order: the session id, then the MAC, and only then the sequence number,
/// which must be greater than the last one accepted, and the service it carries. Nothing a forged or replayed frame
/// carries changes the session (P2). A session never outlives its key: beginning again after it closed draws a fresh
/// key pair, and the sequence starts again at zero (P3).
/// @reference KNX System Specifications, 03/08/09 "KNXnet/IP Security"; xknx 3.20.0 `SecureSession`.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/connection.hpp>
        #include <kmx/aio/knx/secure/common.hpp>
        #include <kmx/aio/knx/secure/credentials.hpp>
        #include <kmx/aio/knx/secure/entropy_source.hpp>
        #include <kmx/aio/knx/secure/session.hpp>
        #include <kmx/aio/knx/secure/wrapper.hpp>

        #include <cstddef>
        #include <cstdint>
        #include <expected>
        #include <optional>
        #include <system_error>
    #endif

namespace kmx::aio::knx::secure
{
    /// @brief How long a session may carry no traffic, in either direction, before it ends.
    inline constexpr std::uint64_t session_timeout_ms = 60'000u;
    /// @brief How long after the client's last wrapper a keep-alive falls due.
    inline constexpr std::uint64_t keep_alive_idle_ms = 50'000u;
    /// @brief The message tag of tunnelling wrappers.
    inline constexpr message_tag_t tunnelling_message_tag {0x00u, 0x00u};

    /// @brief Where a client session is in its life.
    enum class client_session_phase : std::uint8_t
    {
        /// @brief Nothing has been sent.
        idle,
        /// @brief SESSION_REQUEST is out; SESSION_RESPONSE is awaited.
        requested,
        /// @brief SESSION_AUTHENTICATE is out, under the session key; SESSION_STATUS is awaited.
        authenticating,
        /// @brief The session is open and carries the tunnel.
        established,
        /// @brief The session is over, and its keys are wiped.
        closed,
    };

    /// @brief What a received wrapper held.
    struct opened_frame
    {
        /// @brief The length of the plain datagram written to the destination.
        std::size_t size {};
        /// @brief Whether the datagram is the tunnel's; a SESSION_STATUS is the session's own and has been applied.
        bool for_tunnel {};
    };

    /// @brief An opened wrapper, or why it was refused.
    using opened_frame_result_t = std::expected<opened_frame, std::error_code>;

    /// @brief One client's KNX IP Secure tunnelling session.
    /// @note Not thread-safe: its owner serialises every call.
    class client_session final
    {
    public:
        /// @brief Creates an idle session.
        /// @param credentials The user id, keys and serial number to open sessions with.
        /// @param entropy Where key pairs come from; must outlive this object.
        client_session(tunnelling_credentials credentials, entropy_source& entropy) noexcept;
        client_session(const client_session&) = delete;
        client_session& operator=(const client_session&) = delete;

        /// @brief Starts a session: draws a key pair and encodes SESSION_REQUEST.
        /// @param destination Receives the request; at least @ref session_request_size.
        /// @param control_endpoint The HPAI the request names; over TCP, the TCP HPAI.
        /// @param now_ms The monotonic time.
        /// @return The request's length.
        /// @retval kmx::aio::knx::error::invalid_configuration A session is under way, or the credentials name user 0, a
        ///         user above 127, or an all-zero serial number.
        /// @retval kmx::aio::knx::error::crypto_failure No key pair could be drawn.
        [[nodiscard]] expected_size_t begin(span_uint8_t destination, const hpai& control_endpoint, std::uint64_t now_ms) noexcept;

        /// @brief Applies SESSION_RESPONSE and encodes SESSION_AUTHENTICATE, sealed as the session's first wrapper.
        /// @param response The decoded response.
        /// @param destination Receives the wrapped authentication.
        /// @param now_ms The monotonic time.
        /// @return The wrapper's length.
        /// @retval kmx::aio::knx::error::secure_authentication_failed The response's MAC does not verify; nothing is sent
        ///         and the session is closed.
        /// @retval kmx::aio::knx::error::secure_session_rejected The response names session id zero; the session is
        ///         closed.
        /// @retval kmx::aio::knx::error::invalid_configuration No SESSION_REQUEST is outstanding.
        [[nodiscard]] expected_size_t on_session_response(const session_response_frame& response, span_uint8_t destination,
                                                          std::uint64_t now_ms) noexcept;

        /// @brief Applies a SESSION_STATUS that arrived unwrapped.
        /// @param status The decoded status.
        /// @return Never nothing.
        /// @retval kmx::aio::knx::error::secure_session_rejected The server refused before any key existed; the session
        ///         is closed.
        /// @retval kmx::aio::knx::error::secure_frame_required The session has a key, so a status must come wrapped; it is
        ///         counted and ignored.
        [[nodiscard]] expected_void_t on_unwrapped_status(const session_status_frame& status) noexcept;

        /// @brief Authenticates and decrypts a received wrapper, and applies it when it is a SESSION_STATUS.
        /// @param wrapper The decoded wrapper.
        /// @param destination Receives the plain datagram.
        /// @param now_ms The monotonic time.
        /// @return What the wrapper held.
        /// @retval kmx::aio::knx::error::secure_authentication_failed Another session's id, or a MAC that does not verify.
        /// @retval kmx::aio::knx::error::secure_replay A sequence number not above the last one accepted.
        /// @retval kmx::aio::knx::error::unsupported_service A wrapped wrapper, or a service that may not be wrapped.
        /// @retval kmx::aio::knx::error::secure_session_rejected The server refused the authentication; the session is
        ///         closed.
        /// @retval kmx::aio::knx::error::secure_session_closed The server closed the session, or reported it timed out
        ///         or unauthenticated; the session is closed.
        /// @note A refused wrapper is counted and changes nothing else.
        [[nodiscard]] opened_frame_result_t open(const wrapper_frame& wrapper, span_uint8_t destination, std::uint64_t now_ms) noexcept;

        /// @brief Seals a tunnel datagram under the next sequence number.
        /// @param plain_frame The complete datagram.
        /// @param destination Receives the wrapper.
        /// @param now_ms The monotonic time.
        /// @return The wrapper's length.
        /// @retval kmx::aio::knx::error::secure_session_closed The session is over, or its sequence numbers are spent.
        /// @retval kmx::aio::knx::error::invalid_configuration The session is not established yet.
        [[nodiscard]] expected_size_t seal(cspan_uint8_t plain_frame, span_uint8_t destination, std::uint64_t now_ms) noexcept;

        /// @brief Seals a SESSION_STATUS keep-alive.
        /// @param destination Receives the wrapper.
        /// @param now_ms The monotonic time.
        /// @return The wrapper's length, or the errors of @ref seal.
        [[nodiscard]] expected_size_t prepare_keep_alive(span_uint8_t destination, std::uint64_t now_ms) noexcept;

        /// @brief Seals a SESSION_STATUS close and closes the session.
        /// @param destination Receives the wrapper.
        /// @param now_ms The monotonic time.
        /// @return The wrapper's length.
        /// @retval kmx::aio::knx::error::invalid_configuration No session key exists to seal the close under.
        [[nodiscard]] expected_size_t prepare_close(span_uint8_t destination, std::uint64_t now_ms) noexcept;

        /// @brief Indicates whether a keep-alive is due: @ref keep_alive_idle_ms since the client's last wrapper.
        [[nodiscard]] bool keep_alive_due(std::uint64_t now_ms) const noexcept;

        /// @brief Ends a session that has carried no traffic for @ref session_timeout_ms.
        /// @param now_ms The monotonic time.
        /// @return Nothing while the session is alive or already over; @ref kmx::aio::knx::error::secure_session_closed
        ///         when it has just timed out.
        [[nodiscard]] expected_void_t check_timeout(std::uint64_t now_ms) noexcept;

        /// @brief Counts a frame refused before it could be authenticated - a wrapper too short to read, say.
        void note_unauthenticated() noexcept { ++counters_.authentication_failures; }

        /// @brief Counts a frame refused because it arrived unwrapped.
        void note_unencrypted() noexcept { ++counters_.unencrypted_refused; }

        /// @brief Closes the session locally and wipes its keys; nothing is sent.
        void close() noexcept;

        /// @brief Returns where the session is in its life.
        [[nodiscard]] client_session_phase phase() const noexcept { return phase_; }

        /// @brief Indicates whether the session is established.
        [[nodiscard]] bool established() const noexcept { return phase_ == client_session_phase::established; }

        /// @brief Returns the session id the server gave; zero before SESSION_RESPONSE.
        [[nodiscard]] std::uint16_t session_id() const noexcept { return session_id_; }

        /// @brief Returns what the session has refused and done, across every session it has opened.
        [[nodiscard]] const statistics& counters() const noexcept { return counters_; }

    private:
        /// @brief Checks the response's session id and MAC, counting a failed MAC.
        [[nodiscard]] expected_void_t verify_response(const session_response_frame& response) noexcept;
        /// @brief Derives the session key and seals SESSION_AUTHENTICATE under it.
        [[nodiscard]] expected_size_t authenticate(const session_response_frame& response, span_uint8_t destination,
                                                   std::uint64_t now_ms) noexcept;
        /// @brief Checks a wrapper's session id and MAC and decrypts it, counting a failure.
        [[nodiscard]] expected_size_t authenticate_wrapper(const wrapper_frame& wrapper, span_uint8_t destination) noexcept;
        /// @brief Admits an authenticated datagram: a forward sequence number and a service that may be wrapped.
        [[nodiscard]] expected_void_t admit(const wrapper_frame& wrapper, span_uint8_t plain) noexcept;
        /// @brief Applies a SESSION_STATUS, or hands tunnel traffic back.
        [[nodiscard]] opened_frame_result_t apply(cspan_uint8_t plain) noexcept;
        /// @brief Seals @p plain_frame under the next sequence number, whatever the phase.
        [[nodiscard]] expected_size_t seal_frame(cspan_uint8_t plain_frame, span_uint8_t destination, std::uint64_t now_ms) noexcept;
        /// @brief Seals a SESSION_STATUS carrying @p status.
        [[nodiscard]] expected_size_t seal_status(session_status status, span_uint8_t destination, std::uint64_t now_ms) noexcept;
        /// @brief Closes the session and reports @p reason.
        [[nodiscard]] std::unexpected<std::error_code> fail(std::error_code reason) noexcept;
        /// @brief Wipes the keys and forgets the session, leaving the phase to the caller.
        void forget() noexcept;

        tunnelling_credentials credentials_;
        entropy_source& entropy_;
        client_session_phase phase_ {client_session_phase::idle};
        x25519_key_pair key_pair_ {};
        secret_key session_key_ {};
        std::uint16_t session_id_ {};
        std::uint64_t next_sequence_ {};
        std::optional<std::uint64_t> last_received_sequence_ {};
        std::uint64_t last_sent_ms_ {};
        std::uint64_t last_received_ms_ {};
        statistics counters_ {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
