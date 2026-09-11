/// @file src/kmx/aio/knx/secure/detail/session_crypto.cpp
/// @brief The handshake MACs of a KNX IP Secure session over an injectable crypto backend.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/detail/session_crypto.hpp>
#ifndef PCH
    #include <algorithm>
    #include <array>
#endif

namespace kmx::aio::knx::secure::detail
{
    /// @brief The six header octets a handshake MAC covers, for @p service over @p size octets.
    [[nodiscard]] static constexpr std::array<std::uint8_t, frame::communication_header_size> header_octets(const std::uint16_t service,
                                                                                                            const std::size_t size) noexcept
    {
        return {0x06u,
                0x10u,
                static_cast<std::uint8_t>(service >> 8u),
                static_cast<std::uint8_t>(service & 0xFFu),
                static_cast<std::uint8_t>(size >> 8u),
                static_cast<std::uint8_t>(size & 0xFFu)};
    }

    x25519_public_key_t public_keys_xor(const x25519_public_key_t& client_public_key, const x25519_public_key_t& server_public_key) noexcept
    {
        x25519_public_key_t result {};
        for (std::size_t index {}; index < result.size(); ++index)
            result[index] = client_public_key[index] ^ server_public_key[index];
        return result;
    }

    session_mac_result_t handshake_mac(const cipher& with, const cspan_uint8_t associated_data) noexcept
    {
        auto mac = cbc_mac(with, block_t {}, associated_data, {});
        if (!mac.has_value())
            return std::unexpected(mac.error());
        if (const auto encrypted = ctr(with, handshake_counter_0(), *mac, {}); !encrypted.has_value())
            return std::unexpected(encrypted.error());
        return *mac;
    }

    session_mac_result_t basic_session_response_mac(const cipher& with, const std::uint16_t session_id,
                                                    const x25519_public_key_t& client_public_key,
                                                    const x25519_public_key_t& server_public_key) noexcept
    {
        // The response's header, the session id, and the XOR of the two public keys.
        std::array<std::uint8_t, frame::communication_header_size + 2u + x25519_key_size> associated {};
        std::ranges::copy(header_octets(session_response_service, session_response_size), associated.begin());
        associated[6u] = static_cast<std::uint8_t>(session_id >> 8u);
        associated[7u] = static_cast<std::uint8_t>(session_id & 0xFFu);
        std::ranges::copy(public_keys_xor(client_public_key, server_public_key), associated.begin() + 8u);
        return handshake_mac(with, associated);
    }

    session_mac_result_t basic_session_authenticate_mac(const cipher& with, const std::uint8_t user_id,
                                                        const x25519_public_key_t& client_public_key,
                                                        const x25519_public_key_t& server_public_key) noexcept
    {
        // The authentication's header, a reserved zero octet, the user id, and the XOR of the two public keys.
        std::array<std::uint8_t, frame::communication_header_size + 2u + x25519_key_size> associated {};
        std::ranges::copy(header_octets(session_authenticate_service, session_authenticate_size), associated.begin());
        associated[7u] = user_id;
        std::ranges::copy(public_keys_xor(client_public_key, server_public_key), associated.begin() + 8u);
        return handshake_mac(with, associated);
    }
}
