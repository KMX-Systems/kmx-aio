/// @file api/kmx/aio/knx/secure/entropy_source.hpp
/// @brief Where KNX Secure endpoints get randomness and X25519 key pairs.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// Randomness reaches every secure state machine through @ref kmx::aio::knx::secure::entropy_source, passed
/// to the endpoint that needs it. There is deliberately no process-wide switch: a settable global would let
/// any code in the process replace the randomness of every secure endpoint at once. A test that needs a fixed
/// key pair - to reproduce a published handshake - passes its own source to the one object under test.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/secure/key.hpp>
    #endif

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
}
#endif // KMX_AIO_FEATURE_KNX
