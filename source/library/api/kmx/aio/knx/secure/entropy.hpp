/// @file api/kmx/aio/knx/secure/entropy.hpp
/// @brief The system entropy source of KNX Secure endpoints, and X25519 public key derivation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/knx/secure/entropy_source.hpp>
        #include <kmx/aio/knx/secure/key.hpp>

        #include <expected>
        #include <system_error>
    #endif

namespace kmx::aio::knx::secure
{
    /// @brief Returns the source backed by the project's TLS backend.
    /// @note Stateless, so one instance serves every endpoint and every thread.
    [[nodiscard]] entropy_source& system_entropy() noexcept;

    /// @brief An X25519 public key, or the error explaining why none was produced.
    using x25519_public_key_result_t = std::expected<x25519_public_key_t, std::error_code>;

    /// @brief Derives the public half of an X25519 private key.
    /// @param private_key The private key.
    /// @return The public key, or @ref kmx::aio::knx::error::crypto_failure.
    [[nodiscard]] x25519_public_key_result_t derive_x25519_public_key(const x25519_private_key& private_key) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
