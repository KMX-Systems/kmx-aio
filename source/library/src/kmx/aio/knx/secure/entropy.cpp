/// @file src/kmx/aio/knx/secure/entropy.cpp
/// @brief The system entropy source instance and X25519 public key derivation, over the project's TLS backend.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/entropy.hpp>
#ifndef PCH
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/secure/detail/crypto.hpp>
    #include <kmx/aio/knx/secure/detail/system_entropy_source.hpp>
#endif

namespace kmx::aio::knx::secure
{
    entropy_source& system_entropy() noexcept
    {
        static detail::system_entropy_source instance {};
        return instance;
    }

    x25519_public_key_result_t derive_x25519_public_key(const x25519_private_key& private_key) noexcept
    {
        x25519_public_key_t public_key {};
        if (!detail::evp_backend().x25519_public(private_key.bytes(), public_key))
            return std::unexpected(make_error_code(error::crypto_failure));
        return public_key;
    }
}
