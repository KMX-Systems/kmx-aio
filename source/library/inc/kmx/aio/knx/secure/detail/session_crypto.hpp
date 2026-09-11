/// @file kmx/aio/knx/secure/detail/session_crypto.hpp
/// @brief The handshake MACs of a KNX IP Secure session over an injectable crypto backend.
/// @details The public functions in `kmx/aio/knx/secure/session.hpp` call these with the EVP backend; tests pass a
/// failing backend to reach the error paths.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #include <kmx/aio/knx/secure/detail/crypto.hpp>
    #include <kmx/aio/knx/secure/session.hpp>

namespace kmx::aio::knx::secure::detail
{
    /// @brief Returns the XOR of the two public keys, which both handshake MACs cover.
    [[nodiscard]] x25519_public_key_t public_keys_xor(const x25519_public_key_t& client_public_key,
                                                      const x25519_public_key_t& server_public_key) noexcept;

    /// @brief Computes a handshake MAC: CBC-MAC under a zero B0 over @p associated_data, then CTR under the handshake
    ///        counter block.
    [[nodiscard]] session_mac_result_t handshake_mac(const crypto_backend& backend, const secret_key& key,
                                                     cspan_uint8_t associated_data) noexcept;

    /// @brief @ref kmx::aio::knx::secure::session_response_mac over @p backend.
    [[nodiscard]] session_mac_result_t basic_session_response_mac(const crypto_backend& backend,
                                                                  const secret_key& device_authentication_code, std::uint16_t session_id,
                                                                  const x25519_public_key_t& client_public_key,
                                                                  const x25519_public_key_t& server_public_key) noexcept;

    /// @brief @ref kmx::aio::knx::secure::session_authenticate_mac over @p backend.
    [[nodiscard]] session_mac_result_t basic_session_authenticate_mac(const crypto_backend& backend, const secret_key& user_password_key,
                                                                      std::uint8_t user_id, const x25519_public_key_t& client_public_key,
                                                                      const x25519_public_key_t& server_public_key) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
