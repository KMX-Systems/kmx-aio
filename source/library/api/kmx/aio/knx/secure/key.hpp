/// @file api/kmx/aio/knx/secure/key.hpp
/// @brief Key material for KNX Secure: the key types, held in secrets that wipe themselves, and the password derivations.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// Every symmetric key KNX Secure uses - a session key, a backbone key, a group key, the codes derived from a
/// password - is sixteen octets, and an X25519 private key is thirty-two. They are held in
/// @ref kmx::aio::knx::secure::secret_bytes, which zeroes its octets when it is destroyed or moved from and
/// cannot be copied by accident, so a key does not linger in memory the library has finished with.
/// @reference KNX System Specifications, 03/08/09 "KNXnet/IP Security", key derivation.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/knx/secure/secret_bytes.hpp>

        #include <array>
        #include <cstddef>
        #include <cstdint>
        #include <expected>
        #include <string_view>
        #include <system_error>
    #endif

namespace kmx::aio::knx::secure
{
    /// @brief Width of every symmetric KNX Secure key, in octets.
    inline constexpr std::size_t key_size = 16u;
    /// @brief A 16-octet AES-128 key.
    using secret_key = secret_bytes<key_size>;
    /// @brief A secret key, or the error explaining why none was produced.
    using secret_key_result_t = std::expected<secret_key, std::error_code>;

    /// @brief Width of an X25519 key, private or public, in octets.
    inline constexpr std::size_t x25519_key_size = 32u;
    /// @brief An X25519 private key.
    using x25519_private_key = secret_bytes<x25519_key_size>;
    /// @brief An X25519 public key; public, so an ordinary array.
    using x25519_public_key_t = std::array<std::uint8_t, x25519_key_size>;

    /// @brief One X25519 key pair.
    struct x25519_key_pair
    {
        /// @brief The private half, wiped with the pair.
        x25519_private_key private_key {};
        /// @brief The public half, sent to the peer.
        x25519_public_key_t public_key {};
    };

    /// @brief A key pair, or the error explaining why none was produced.
    using x25519_key_pair_result_t = std::expected<x25519_key_pair, std::error_code>;

    /// @brief Derives the key a KNX IP Secure user password stands for.
    /// @param password The password octets, as the user typed them.
    /// @return The 16-octet key, or @ref kmx::aio::knx::error::crypto_failure.
    /// @details PBKDF2-HMAC-SHA256 over the salt `user-password.1.secure.ip.knx.org`, 65 536 iterations. That
    ///          is tens of milliseconds of work: call it when configuration is built, never per frame.
    [[nodiscard]] secret_key_result_t derive_user_password_key(std::string_view password) noexcept;

    /// @brief Derives a KNX IP Secure device authentication code from its password.
    /// @param password The device authentication password octets.
    /// @return The 16-octet code, or @ref kmx::aio::knx::error::crypto_failure.
    /// @details PBKDF2-HMAC-SHA256 over the salt `device-authentication-code.1.secure.ip.knx.org`, 65 536
    ///          iterations.
    [[nodiscard]] secret_key_result_t derive_device_authentication_code(std::string_view password) noexcept;

    /// @brief Derives the key an ETS keyring's password stands for.
    /// @param password The keyring password octets.
    /// @return The 16-octet hash, or @ref kmx::aio::knx::error::crypto_failure.
    /// @details PBKDF2-HMAC-SHA256 over the salt `1.keyring.ets.knx.org`, 65 536 iterations.
    [[nodiscard]] secret_key_result_t derive_keyring_password_hash(std::string_view password) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
