/// @file inc/kmx/aio/knx/secure/detail/ccm.hpp
/// @brief KNX's authenticated encryption: a CBC-MAC and AES-CTR composition that is not RFC 3610 CCM.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// KNX IP Secure and KNX Data Secure authenticate with a CBC-MAC computed over
/// `B0 || len16(A) || A || P`, zero padded as a whole to a multiple of sixteen octets, with a zero IV, and
/// encrypt with AES-128-CTR from a first counter block: the first keystream block encrypts the MAC and the
/// following blocks encrypt the payload. RFC 3610 puts flags in B0 and pads the associated data separately
/// from the payload, so neither OpenSSL's CCM mode nor BoringSSL's fixed-parameter CCM AEADs compute this,
/// and it is composed here from the two primitives instead.
///
/// Because this composition is written here rather than taken from a library, it is the part checked
/// against published vectors: the AN159 examples and xknx's handshake.
/// @reference KNX System Specifications, 03/08/09 "KNXnet/IP Security"; KNX AN159 "KNXnet/IP Secure".
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/secure/common.hpp>
        #include <kmx/aio/knx/secure/detail/crypto.hpp>
        #include <kmx/aio/knx/secure/key.hpp>

        #include <array>
        #include <cstdint>
        #include <expected>
        #include <system_error>
    #endif

namespace kmx::aio::knx::secure::detail
{
    /// @brief One AES block: a B0, a counter block, or a MAC.
    using block_t = std::array<std::uint8_t, aes_block_size>;
    /// @brief A MAC, or the error explaining why none was computed.
    using mac_result_t = std::expected<block_t, std::error_code>;

    /// @brief Largest `B0 || len16(A) || A || P` a MAC is computed over, padded, in octets.
    /// @details Comfortably above the largest KNXnet/IP frame a SECURE_WRAPPER can carry inside one buffered
    ///          datagram; a longer input is refused as `invalid_length` rather than truncated.
    inline constexpr std::size_t max_mac_input_size = 2048u;

    /// @brief The primitives one operation runs on, and the key it runs under.
    /// @details Every operation of the composition takes both, and a test swaps the backend alone to reach the failure paths.
    struct cipher
    {
        /// @brief The primitives to use.
        const crypto_backend& backend;
        /// @brief The key.
        const secret_key& key;
    };

    /// @brief One message the composition protects: the blocks its MAC and keystream start from, and the octets they cover.
    struct message
    {
        /// @brief B0.
        block_t block_0 {};
        /// @brief The first counter block, whose keystream covers the MAC.
        block_t counter_0 {};
        /// @brief A, authenticated but not encrypted.
        cspan_uint8_t associated_data {};
        /// @brief P, encrypted or decrypted in place.
        span_uint8_t payload {};
    };

    /// @brief Computes the KNX CBC-MAC.
    /// @param with The primitives and the key to MAC under.
    /// @param block_0 The first block, B0.
    /// @param associated_data A, authenticated but not encrypted.
    /// @param payload P, authenticated and, in @ref seal, encrypted.
    /// @return The unencrypted MAC.
    /// @retval kmx::aio::knx::error::invalid_length The input exceeds @ref max_mac_input_size.
    /// @retval kmx::aio::knx::error::crypto_failure The backend failed.
    [[nodiscard]] mac_result_t cbc_mac(const cipher& with, const block_t& block_0, cspan_uint8_t associated_data,
                                       cspan_uint8_t payload) noexcept;

    /// @brief Applies AES-128-CTR to a MAC and a payload, in place; encryption and decryption alike.
    /// @param with The primitives and the key.
    /// @param counter_0 The first counter block, whose keystream covers @p mac.
    /// @param mac The MAC octets.
    /// @param payload The payload octets, covered by the counter blocks after the first.
    /// @return Nothing, or @ref kmx::aio::knx::error::crypto_failure.
    [[nodiscard]] expected_void_t ctr(const cipher& with, const block_t& counter_0, span_uint8_t mac, span_uint8_t payload) noexcept;

    /// @brief MACs then encrypts: the payload is encrypted in place and the encrypted MAC is returned.
    /// @param with The primitives and the key.
    /// @param value B0, the first counter block, A, and P on entry, its ciphertext on return.
    /// @return The encrypted MAC.
    [[nodiscard]] mac_result_t seal(const cipher& with, const message& value) noexcept;

    /// @brief Decrypts and verifies: the payload is decrypted in place, then the MAC is checked.
    /// @param with The primitives and the key.
    /// @param value B0 and the first counter block, built from the received fields; A, as received; and the ciphertext on
    ///        entry, which is the plaintext on success and all zero on failure.
    /// @param mac The encrypted MAC as received.
    /// @return Nothing when the frame is authentic.
    /// @retval kmx::aio::knx::error::secure_authentication_failed The MAC does not verify, whichever field
    ///         was altered.
    /// @retval kmx::aio::knx::error::crypto_failure The backend failed.
    [[nodiscard]] expected_void_t open(const cipher& with, const message& value, const block_t& mac) noexcept;

    /// @brief Builds B0 for a SECURE_WRAPPER or a TIMER_NOTIFY.
    /// @param sequence The sequence information, or the timer value of a TIMER_NOTIFY.
    /// @param serial_number The sender's serial number.
    /// @param message_tag The message tag.
    /// @param payload_length The plain frame's length; zero for a TIMER_NOTIFY, which carries none.
    /// @return `sequence || serial || tag || len16(payload)`.
    [[nodiscard]] block_t wrapper_block_0(const sequence_information_t& sequence, const serial_number_t& serial_number,
                                          const message_tag_t& message_tag, std::uint16_t payload_length) noexcept;

    /// @brief Builds the first counter block for a SECURE_WRAPPER or a TIMER_NOTIFY.
    /// @param sequence The sequence information, or the timer value.
    /// @param serial_number The sender's serial number.
    /// @param message_tag The message tag.
    /// @return `sequence || serial || tag || FF 00`.
    [[nodiscard]] block_t wrapper_counter_0(const sequence_information_t& sequence, const serial_number_t& serial_number,
                                            const message_tag_t& message_tag) noexcept;

    /// @brief The first counter block of the SESSION_RESPONSE and SESSION_AUTHENTICATE MACs.
    /// @return Fourteen zero octets, then `FF 00`.
    [[nodiscard]] constexpr block_t handshake_counter_0() noexcept
    {
        block_t result {};
        result[14u] = 0xFFu;
        return result;
    }

    /// @brief Derives a session key from an X25519 agreement: the first sixteen octets of its SHA-256.
    /// @param backend The primitives to use.
    /// @param private_key This side's private key.
    /// @param peer_public_key The peer's public key.
    /// @return The session key.
    /// @retval kmx::aio::knx::error::crypto_failure The agreement failed, including for a low-order peer key.
    [[nodiscard]] secret_key_result_t derive_session_key(const crypto_backend& backend, const x25519_private_key& private_key,
                                                         const x25519_public_key_t& peer_public_key) noexcept;

    /// @brief Derives a 16-octet key from a password with PBKDF2-HMAC-SHA256 at 65 536 iterations.
    /// @param backend The primitives to use.
    /// @param password The password octets.
    /// @param salt The salt naming what the key is for.
    /// @return The key, or @ref kmx::aio::knx::error::crypto_failure.
    [[nodiscard]] secret_key_result_t derive_password_key(const crypto_backend& backend, std::string_view password,
                                                          std::string_view salt) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
