/// @file kmx/aio/knx/secure/detail/keyring_format.hpp
/// @brief The cryptographic layer of an ETS keyring: base64, the signature, and key and password decryption.
/// @details
/// An ETS keyring (`.knxkeys`) signs itself and encrypts every key and password it holds. The signature is
/// the first sixteen octets of a SHA-256 over a canonical stream built from the document's elements: for each
/// start element `01`, its name, then its attributes sorted by name - `xmlns` and `Signature` excluded - as
/// name and value; for each end element `02`; and at the end the base64 text of the keyring password hash.
/// Every string carries a one-octet length prefix, so a string longer than 255 octets cannot be signed and a
/// document containing one is refused.
///
/// Keys and passwords are AES-128-CBC encrypted under the password hash with an IV of the first sixteen
/// octets of SHA-256 over the keyring's `Created` attribute. A key is one block. A password is eight random
/// octets, the password, then padding whose last octet counts it. ETS does not fill the rest of the padding
/// with the count, and ETS 5.7.5 and earlier pad every password to two blocks, so the count can exceed one
/// block: only that last octet is read, and it is bounded by what follows the prefix, not by the block size.
/// @reference xknx 3.20.0 `xknx/secure/keyring.py`, whose fixtures the tests pin these rules to.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <expected>
        #include <string>
        #include <string_view>
        #include <system_error>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/secure/detail/ccm.hpp>
    #include <kmx/aio/knx/secure/detail/crypto.hpp>
    #include <kmx/aio/knx/secure/detail/xml_reader.hpp>
    #include <kmx/aio/knx/secure/key.hpp>

namespace kmx::aio::knx::secure::detail
{
    /// @brief Decoded octets, or why they could not be decoded.
    using octets_result_t = std::expected<byte_buffer_t, std::error_code>;
    /// @brief A decrypted password, or why it could not be decrypted.
    using secret_string_result_t = std::expected<secret_string, std::error_code>;

    /// @brief Decodes RFC 4648 base64 with padding.
    /// @param text The base64 text; its length must be a multiple of four.
    /// @return The octets, or @ref kmx::aio::knx::error::malformed_frame.
    [[nodiscard]] octets_result_t base64_decode(std::string_view text) noexcept(false);

    /// @brief Encodes octets as RFC 4648 base64 with padding.
    /// @param octets The octets.
    /// @return The base64 text.
    [[nodiscard]] std::string base64_encode(cspan_uint8_t octets) noexcept(false);

    /// @brief Builds the canonical stream an ETS keyring signature is computed over.
    /// @param events The document's element events.
    /// @param password_hash The keyring password hash, whose base64 text ends the stream.
    /// @return The stream, or @ref kmx::aio::knx::error::malformed_frame when a string exceeds 255 octets.
    /// @warning The stream ends with the base64 text of the password hash; wipe it once hashed.
    [[nodiscard]] octets_result_t keyring_signature_stream(const xml_events_t& events, const secret_key& password_hash) noexcept(false);

    /// @brief Verifies a keyring's signature against its password hash.
    /// @param backend The primitives to use.
    /// @param events The document's element events; the first is the root carrying `Signature`.
    /// @param password_hash The keyring password hash.
    /// @return Nothing when the signature verifies.
    /// @retval kmx::aio::knx::error::keyring_signature_invalid The password is wrong or the document was altered.
    /// @retval kmx::aio::knx::error::malformed_frame The root carries no usable signature.
    [[nodiscard]] expected_void_t verify_keyring_signature(const crypto_backend& backend, const xml_events_t& events,
                                                           const secret_key& password_hash) noexcept(false);

    /// @brief Derives the IV keyring values are encrypted under.
    /// @param backend The primitives to use.
    /// @param created The keyring's `Created` attribute.
    /// @return The first sixteen octets of its SHA-256, or @ref kmx::aio::knx::error::crypto_failure.
    [[nodiscard]] mac_result_t keyring_initialisation_vector(const crypto_backend& backend, std::string_view created) noexcept;

    /// @brief Decrypts a 16-octet key from its base64 attribute.
    /// @param backend The primitives to use.
    /// @param encoded The attribute value.
    /// @param password_hash The keyring password hash.
    /// @param iv The keyring IV.
    /// @return The key.
    /// @retval kmx::aio::knx::error::malformed_frame The value is not base64 of exactly one block.
    [[nodiscard]] secret_key_result_t decrypt_keyring_key(const crypto_backend& backend, std::string_view encoded,
                                                          const secret_key& password_hash, const block_t& iv) noexcept(false);

    /// @brief Decrypts a password from its base64 attribute.
    /// @param backend The primitives to use.
    /// @param encoded The attribute value.
    /// @param password_hash The keyring password hash.
    /// @param iv The keyring IV.
    /// @return The password, its random prefix and padding removed.
    /// @retval kmx::aio::knx::error::malformed_frame The value is not whole blocks, or its padding count
    ///         is zero or longer than what follows the prefix.
    [[nodiscard]] secret_string_result_t decrypt_keyring_password(const crypto_backend& backend, std::string_view encoded,
                                                                  const secret_key& password_hash, const block_t& iv) noexcept(false);
}
#endif // KMX_AIO_FEATURE_KNX
