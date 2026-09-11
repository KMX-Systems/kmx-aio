/// @file src/kmx/aio/knx/secure/detail/system_entropy_source.cpp
/// @brief The KNX Secure entropy source over the project's TLS backend: random octets and fresh X25519 key pairs.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/detail/system_entropy_source.hpp>
#ifndef PCH
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/secure/detail/crypto.hpp>

    #include <expected>
#endif

namespace kmx::aio::knx::secure::detail
{
    expected_void_t system_entropy_source::fill(const span_uint8_t destination) noexcept
    {
        if (!detail::evp_backend().random(destination))
            return std::unexpected(make_error_code(error::crypto_failure));
        return {};
    }

    x25519_key_pair_result_t system_entropy_source::generate_key_pair() noexcept
    {
        // X25519 clamps the scalar itself, so thirty-two uniformly random octets are a private key as they
        // stand; the public half then follows from the same call a fixed test key goes through.
        x25519_key_pair pair {};
        const auto& backend = detail::evp_backend();
        if (!backend.random(pair.private_key.mutable_bytes()) || !backend.x25519_public(pair.private_key.bytes(), pair.public_key))
            return std::unexpected(make_error_code(error::crypto_failure));
        return pair;
    }
}
