/// @file inc/kmx/aio/knx/secure/detail/session_crypto.hpp
/// @brief The handshake MACs of a KNX IP Secure session over an injectable crypto backend.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details The public functions in `kmx/aio/knx/secure/session.hpp` call these with the EVP backend; tests pass a
/// failing backend to reach the error paths.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/knx/secure/detail/ccm.hpp>
        #include <kmx/aio/knx/secure/session.hpp>
    #endif

namespace kmx::aio::knx::secure::detail
{
    /// @brief Returns the XOR of the two public keys, which both handshake MACs cover.
    [[nodiscard]] x25519_public_key_t public_keys_xor(const x25519_public_key_t& client_public_key,
                                                      const x25519_public_key_t& server_public_key) noexcept;

    /// @brief Computes a handshake MAC: CBC-MAC under a zero B0 over @p associated_data, then CTR under the handshake
    ///        counter block.
    /// @param with The primitives and the key the MAC is computed under.
    /// @param associated_data The octets the MAC covers.
    [[nodiscard]] session_mac_result_t handshake_mac(const cipher& with, cspan_uint8_t associated_data) noexcept;

    /// @brief @ref kmx::aio::knx::secure::session_response_mac over @p with, whose key is the device authentication code.
    [[nodiscard]] session_mac_result_t basic_session_response_mac(const cipher& with, std::uint16_t session_id,
                                                                  const x25519_public_key_t& client_public_key,
                                                                  const x25519_public_key_t& server_public_key) noexcept;

    /// @brief @ref kmx::aio::knx::secure::session_authenticate_mac over @p with, whose key is the user password key.
    [[nodiscard]] session_mac_result_t basic_session_authenticate_mac(const cipher& with, std::uint8_t user_id,
                                                                      const x25519_public_key_t& client_public_key,
                                                                      const x25519_public_key_t& server_public_key) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
