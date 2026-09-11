/// @file aio/knx/secure/session.hpp
/// @brief The KNX IP Secure session services: SESSION_REQUEST, SESSION_RESPONSE, SESSION_AUTHENTICATE and SESSION_STATUS.
/// @details
/// A secure tunnelling session opens with a Diffie-Hellman exchange. The client sends SESSION_REQUEST with its X25519
/// public key. The server answers SESSION_RESPONSE with a session id, its own public key, and a MAC under the device
/// authentication code, which proves the client is talking to the interface it was configured for. Both ends derive the
/// session key from the shared secret. The client then proves its user with SESSION_AUTHENTICATE, a MAC under the user
/// password key, and the server answers SESSION_STATUS. From SESSION_AUTHENTICATE on, every frame travels inside a
/// SECURE_WRAPPER under the session key.
///
/// The codecs here only move octets, and the MAC functions only compute and check them. Ordering the exchange - and
/// refusing to go on after a check fails - is the session state machine's job.
/// @reference KNX System Specifications, 03/08/09 "KNXnet/IP Security"; KNX AN159.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <cstddef>
        #include <cstdint>
        #include <expected>
        #include <system_error>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/connection.hpp>
    #include <kmx/aio/knx/frame.hpp>
    #include <kmx/aio/knx/secure/common.hpp>
    #include <kmx/aio/knx/secure/key.hpp>

namespace kmx::aio::knx::secure
{
    /// @brief The service type of SESSION_REQUEST.
    inline constexpr std::uint16_t session_request_service = 0x0951u;
    /// @brief The service type of SESSION_RESPONSE.
    inline constexpr std::uint16_t session_response_service = 0x0952u;
    /// @brief The service type of SESSION_AUTHENTICATE.
    inline constexpr std::uint16_t session_authenticate_service = 0x0953u;
    /// @brief The service type of SESSION_STATUS.
    inline constexpr std::uint16_t session_status_service = 0x0954u;

    /// @brief A SESSION_REQUEST in octets: header, control endpoint HPAI and the client's public key.
    inline constexpr std::size_t session_request_size = frame::communication_header_size + connection::hpai_size + x25519_key_size;
    /// @brief A SESSION_RESPONSE in octets: header, session id, the server's public key and the MAC.
    inline constexpr std::size_t session_response_size = frame::communication_header_size + 2u + x25519_key_size + mac_size;
    /// @brief A SESSION_AUTHENTICATE in octets: header, a reserved octet, the user id and the MAC.
    inline constexpr std::size_t session_authenticate_size = frame::communication_header_size + 2u + mac_size;
    /// @brief A SESSION_STATUS in octets: header, the status and a reserved octet.
    inline constexpr std::size_t session_status_size = frame::communication_header_size + 2u;

    /// @brief What a SESSION_STATUS reports.
    enum class session_status : std::uint8_t
    {
        /// @brief The user was authenticated; the session is established.
        authentication_success = 0x00u,
        /// @brief The handshake failed: a MAC did not verify, or the user is not allowed.
        authentication_failed = 0x01u,
        /// @brief The session is not authenticated, or not any more.
        unauthenticated = 0x02u,
        /// @brief The session timed out.
        timeout = 0x03u,
        /// @brief The session is alive; it carries nothing else.
        keepalive = 0x04u,
        /// @brief The session is being closed.
        close = 0x05u,
    };

    /// @brief A SESSION_REQUEST: a client asking for a secure session.
    struct session_request_frame
    {
        /// @brief Where the server answers; over TCP, the TCP HPAI.
        hpai control_endpoint {ipv4_endpoint {}, 0x02u};
        /// @brief The client's X25519 public key for this session.
        x25519_public_key_t client_public_key {};
    };

    /// @brief A SESSION_RESPONSE: the server's half of the key exchange.
    struct session_response_frame
    {
        /// @brief The session id every wrapper of the session carries; never zero, which routing uses.
        std::uint16_t session_id {};
        /// @brief The server's X25519 public key for this session.
        x25519_public_key_t server_public_key {};
        /// @brief The MAC under the device authentication code; see @ref verify_session_response.
        mac_t mac {};
    };

    /// @brief A SESSION_AUTHENTICATE: the client proving its user.
    struct session_authenticate_frame
    {
        /// @brief The user id, 1 to 127; 1 is the management user.
        std::uint8_t user_id {};
        /// @brief The MAC under the user password key; see @ref verify_session_authenticate.
        mac_t mac {};
    };

    /// @brief A SESSION_STATUS: the outcome of the handshake, a keep-alive, or the end of the session.
    struct session_status_frame
    {
        /// @brief What is being reported.
        session_status status {session_status::keepalive};
    };

    /// @brief A decoded SESSION_REQUEST, or why the octets could not be read.
    using session_request_result_t = std::expected<session_request_frame, std::error_code>;
    /// @brief A decoded SESSION_RESPONSE, or why the octets could not be read.
    using session_response_result_t = std::expected<session_response_frame, std::error_code>;
    /// @brief A decoded SESSION_AUTHENTICATE, or why the octets could not be read.
    using session_authenticate_result_t = std::expected<session_authenticate_frame, std::error_code>;
    /// @brief A decoded SESSION_STATUS, or why the octets could not be read.
    using session_status_result_t = std::expected<session_status_frame, std::error_code>;
    /// @brief A handshake MAC, or why it could not be computed.
    using session_mac_result_t = std::expected<mac_t, std::error_code>;

    /// @brief Encodes a SESSION_REQUEST.
    /// @param destination The destination octets; at least @ref session_request_size.
    /// @param value The request.
    /// @return Nothing, or why it could not be encoded.
    /// @retval kmx::aio::knx::error::invalid_length @p destination is too small.
    /// @retval kmx::aio::knx::error::unsupported_hpai The control endpoint names neither UDP nor TCP.
    [[nodiscard]] expected_void_t encode_session_request_packet(span_uint8_t destination, const session_request_frame& value) noexcept;

    /// @brief Decodes a SESSION_REQUEST.
    /// @param packet The received datagram, header included.
    /// @return The request.
    /// @retval kmx::aio::knx::error::malformed_frame The length or the HPAI length is wrong.
    /// @retval kmx::aio::knx::error::unsupported_hpai The control endpoint names neither UDP nor TCP.
    [[nodiscard]] session_request_result_t decode_session_request_packet(cspan_uint8_t packet) noexcept;

    /// @brief Encodes a SESSION_RESPONSE.
    /// @param destination The destination octets; at least @ref session_response_size.
    /// @param value The response.
    /// @return Nothing, or @ref kmx::aio::knx::error::invalid_length when @p destination is too small.
    [[nodiscard]] expected_void_t encode_session_response_packet(span_uint8_t destination, const session_response_frame& value) noexcept;

    /// @brief Decodes a SESSION_RESPONSE; its MAC is not checked here.
    /// @param packet The received datagram, header included.
    /// @return The response, or @ref kmx::aio::knx::error::malformed_frame when the length is wrong.
    [[nodiscard]] session_response_result_t decode_session_response_packet(cspan_uint8_t packet) noexcept;

    /// @brief Encodes a SESSION_AUTHENTICATE.
    /// @param destination The destination octets; at least @ref session_authenticate_size.
    /// @param value The authentication.
    /// @return Nothing, or @ref kmx::aio::knx::error::invalid_length when @p destination is too small.
    [[nodiscard]] expected_void_t encode_session_authenticate_packet(span_uint8_t destination,
                                                                     const session_authenticate_frame& value) noexcept;

    /// @brief Decodes a SESSION_AUTHENTICATE; its MAC is not checked here.
    /// @param packet The received datagram, header included.
    /// @return The authentication, or @ref kmx::aio::knx::error::malformed_frame when the length or the reserved
    ///         octet is wrong.
    [[nodiscard]] session_authenticate_result_t decode_session_authenticate_packet(cspan_uint8_t packet) noexcept;

    /// @brief Encodes a SESSION_STATUS.
    /// @param destination The destination octets; at least @ref session_status_size.
    /// @param value The status.
    /// @return Nothing, or why it could not be encoded.
    /// @retval kmx::aio::knx::error::invalid_length @p destination is too small.
    /// @retval kmx::aio::knx::error::invalid_configuration The status is not one @ref session_status defines.
    [[nodiscard]] expected_void_t encode_session_status_packet(span_uint8_t destination, const session_status_frame& value) noexcept;

    /// @brief Decodes a SESSION_STATUS.
    /// @param packet The received datagram, header included.
    /// @return The status, or @ref kmx::aio::knx::error::malformed_frame when the length is wrong or the status is not
    ///         one @ref session_status defines.
    [[nodiscard]] session_status_result_t decode_session_status_packet(cspan_uint8_t packet) noexcept;

    /// @brief Computes the MAC a SESSION_RESPONSE carries.
    /// @param device_authentication_code The key derived from the interface's device authentication code.
    /// @param session_id The session id the response names.
    /// @param client_public_key The client's public key, from its SESSION_REQUEST.
    /// @param server_public_key The server's public key, from the response.
    /// @return The MAC, or @ref kmx::aio::knx::error::crypto_failure.
    /// @details CBC-MAC under a zero B0 over the response's header, the session id and the XOR of the two public keys,
    ///          then CTR under the handshake counter block.
    [[nodiscard]] session_mac_result_t session_response_mac(const secret_key& device_authentication_code, std::uint16_t session_id,
                                                            const x25519_public_key_t& client_public_key,
                                                            const x25519_public_key_t& server_public_key) noexcept;

    /// @brief Checks the MAC of a SESSION_RESPONSE, in constant time.
    /// @param device_authentication_code The key derived from the interface's device authentication code.
    /// @param response The decoded response.
    /// @param client_public_key The client's public key, from its SESSION_REQUEST.
    /// @return Nothing when the MAC verifies.
    /// @retval kmx::aio::knx::error::secure_authentication_failed The MAC does not verify.
    /// @retval kmx::aio::knx::error::crypto_failure The backend failed.
    [[nodiscard]] expected_void_t verify_session_response(const secret_key& device_authentication_code,
                                                          const session_response_frame& response,
                                                          const x25519_public_key_t& client_public_key) noexcept;

    /// @brief Computes the MAC a SESSION_AUTHENTICATE carries.
    /// @param user_password_key The key derived from the user's password.
    /// @param user_id The user id the authentication names.
    /// @param client_public_key The client's public key.
    /// @param server_public_key The server's public key.
    /// @return The MAC, or @ref kmx::aio::knx::error::crypto_failure.
    /// @details As for @ref session_response_mac, over the authentication's header, a zero octet, the user id and the
    ///          XOR of the two public keys.
    [[nodiscard]] session_mac_result_t session_authenticate_mac(const secret_key& user_password_key, std::uint8_t user_id,
                                                                const x25519_public_key_t& client_public_key,
                                                                const x25519_public_key_t& server_public_key) noexcept;

    /// @brief Checks the MAC of a SESSION_AUTHENTICATE, in constant time.
    /// @param user_password_key The key derived from the user's password.
    /// @param value The decoded authentication.
    /// @param client_public_key The client's public key.
    /// @param server_public_key The server's public key.
    /// @return Nothing when the MAC verifies.
    /// @retval kmx::aio::knx::error::secure_authentication_failed The MAC does not verify.
    /// @retval kmx::aio::knx::error::crypto_failure The backend failed.
    [[nodiscard]] expected_void_t verify_session_authenticate(const secret_key& user_password_key, const session_authenticate_frame& value,
                                                              const x25519_public_key_t& client_public_key,
                                                              const x25519_public_key_t& server_public_key) noexcept;

    /// @brief Derives the session key both ends agree on: the first 16 octets of SHA-256 of the X25519 shared secret.
    /// @param private_key This end's private key.
    /// @param peer_public_key The other end's public key.
    /// @return The session key.
    /// @retval kmx::aio::knx::error::crypto_failure The exchange failed, including against a low-order peer key.
    [[nodiscard]] secret_key_result_t derive_session_key(const x25519_private_key& private_key,
                                                         const x25519_public_key_t& peer_public_key) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
