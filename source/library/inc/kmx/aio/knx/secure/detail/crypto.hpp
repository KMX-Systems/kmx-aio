/// @file inc/kmx/aio/knx/secure/detail/crypto.hpp
/// @brief The cryptographic primitives KNX Secure is built from, as a table of backend calls.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// AES-128, SHA-256, PBKDF2-HMAC-SHA256 and X25519 come from the project's TLS backend - OpenSSL 3, or
/// BoringSSL when QUIC is enabled - through the EVP calls both provide. This header names them without
/// including either, so the backend's headers stay inside one translation unit.
///
/// The table exists for testing rather than for pluggability. Production code always passes
/// @ref kmx::aio::knx::secure::detail::evp_backend; a test passes a table whose entries fail, to reach the
/// error branches of the code built on top. Nothing installs a table globally.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>

        #include <cstddef>
        #include <cstdint>
    #endif

namespace kmx::aio::knx::secure::detail
{
    /// @brief AES block and AES-128 key width, in octets.
    inline constexpr std::size_t aes_block_size = 16u;
    /// @brief SHA-256 digest width, in octets.
    inline constexpr std::size_t sha256_size = 32u;

    /// @brief The backend calls KNX Secure uses; each reports failure as `false` and never throws.
    struct crypto_backend
    {
        /// @brief AES-128-CBC with a zero IV over whole blocks, writing only the last ciphertext block.
        bool (*cbc_mac)(cspan_uint8_t key, cspan_uint8_t blocks, span_uint8_t last_block) noexcept;
        /// @brief AES-128-CTR from @p counter_0, over @p first and then @p second, in place.
        bool (*ctr)(cspan_uint8_t key, cspan_uint8_t counter_0, span_uint8_t first, span_uint8_t second) noexcept;
        /// @brief AES-128-CBC decryption of whole blocks, without padding.
        bool (*cbc_decrypt)(cspan_uint8_t key, cspan_uint8_t iv, cspan_uint8_t input, span_uint8_t output) noexcept;
        /// @brief SHA-256 of @p input into a 32-octet @p digest.
        bool (*sha256)(cspan_uint8_t input, span_uint8_t digest) noexcept;
        /// @brief PBKDF2-HMAC-SHA256, filling @p output.
        bool (*pbkdf2_sha256)(cspan_uint8_t password, cspan_uint8_t salt, std::uint32_t iterations, span_uint8_t output) noexcept;
        /// @brief Cryptographically strong random octets.
        bool (*random)(span_uint8_t output) noexcept;
        /// @brief The X25519 public key of a 32-octet private key.
        bool (*x25519_public)(cspan_uint8_t private_key, span_uint8_t public_key) noexcept;
        /// @brief X25519 key agreement; fails on an all-zero result, which a low-order peer key produces.
        bool (*x25519_derive)(cspan_uint8_t private_key, cspan_uint8_t peer_public_key, span_uint8_t shared) noexcept;
    };

    /// @brief Returns the table backed by OpenSSL or BoringSSL.
    [[nodiscard]] const crypto_backend& evp_backend() noexcept;

    /// @brief Compares two octet runs in time that depends only on their length.
    /// @param lhs The first run.
    /// @param rhs The second run.
    /// @return `true` when both have the same length and the same octets.
    [[nodiscard]] bool constant_time_equal(cspan_uint8_t lhs, cspan_uint8_t rhs) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
