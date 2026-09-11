/// @file aio/knx/secure/entropy.hpp
/// @brief Where KNX Secure endpoints get randomness and X25519 key pairs.
/// @details
/// Randomness reaches every secure state machine through @ref kmx::aio::knx::secure::entropy_source, passed
/// to the endpoint that needs it. There is deliberately no process-wide switch: a settable global would let
/// any code in the process replace the randomness of every secure endpoint at once. A test that needs a fixed
/// key pair - to reproduce a published handshake - passes its own source to the one object under test.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <expected>
        #include <system_error>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/secure/key.hpp>

namespace kmx::aio::knx::secure
{
    /// @brief Where a secure endpoint gets random octets and X25519 key pairs.
    class entropy_source
    {
    public:
        /// @brief Constructs a source.
        entropy_source() noexcept = default;
        entropy_source(const entropy_source&) = delete;
        entropy_source& operator=(const entropy_source&) = delete;
        /// @brief Destroys the source.
        virtual ~entropy_source() noexcept = default;

        /// @brief Fills octets with cryptographically strong random values.
        /// @param destination The octets to fill.
        /// @return Nothing, or @ref kmx::aio::knx::error::crypto_failure.
        [[nodiscard]] virtual expected_void_t fill(span_uint8_t destination) noexcept = 0;

        /// @brief Generates a fresh X25519 key pair.
        /// @return The pair, or @ref kmx::aio::knx::error::crypto_failure.
        [[nodiscard]] virtual x25519_key_pair_result_t generate_key_pair() noexcept = 0;
    };

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
