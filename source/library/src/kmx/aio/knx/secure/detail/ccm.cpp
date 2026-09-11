/// @file src/kmx/aio/knx/secure/detail/ccm.cpp
/// @brief KNX Secure CBC-MAC and AES-CTR authenticated encryption, wrapper block setup and key derivation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/detail/ccm.hpp>
#ifndef PCH
    #include <kmx/aio/knx/error.hpp>

    #include <algorithm>
#endif

namespace kmx::aio::knx::secure::detail
{
    static constexpr std::uint32_t password_iterations = 65536u;

    /// @brief Reports a backend failure the one way every caller expects it.
    [[nodiscard]] static std::error_code backend_failure() noexcept
    {
        return make_error_code(error::crypto_failure);
    }

    /// @brief Views text as the octets a backend call takes.
    [[nodiscard]] static cspan_uint8_t as_octets(const std::string_view text) noexcept
    {
        return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
    }

    mac_result_t cbc_mac(const cipher& with, const block_t& block_0, const cspan_uint8_t associated_data,
                         const cspan_uint8_t payload) noexcept
    {
        const auto unpadded = block_0.size() + 2u + associated_data.size() + payload.size();
        const auto padded = ((unpadded + aes_block_size - 1u) / aes_block_size) * aes_block_size;
        if ((padded > max_mac_input_size) || (associated_data.size() > 0xFFFFu))
            return std::unexpected(make_error_code(error::invalid_length));

        // The whole of B0, the length-prefixed associated data and the payload, zero padded as one run: the
        // padding is what separates this from RFC 3610, which pads the associated data on its own.
        std::array<std::uint8_t, max_mac_input_size> input {};
        auto cursor = std::copy(block_0.begin(), block_0.end(), input.begin());
        *cursor++ = static_cast<std::uint8_t>(associated_data.size() >> 8u);
        *cursor++ = static_cast<std::uint8_t>(associated_data.size() & 0xFFu);
        cursor = std::copy(associated_data.begin(), associated_data.end(), cursor);
        std::copy(payload.begin(), payload.end(), cursor);

        block_t mac {};
        const auto computed = with.backend.cbc_mac(with.key.bytes(), cspan_uint8_t {input.data(), padded}, mac);
        cleanse(span_uint8_t {input.data(), padded});
        if (!computed)
            return std::unexpected(backend_failure());
        return mac;
    }

    expected_void_t ctr(const cipher& with, const block_t& counter_0, const span_uint8_t mac, const span_uint8_t payload) noexcept
    {
        if (!with.backend.ctr(with.key.bytes(), counter_0, mac, payload))
            return std::unexpected(backend_failure());
        return {};
    }

    mac_result_t seal(const cipher& with, const message& value) noexcept
    {
        auto mac = cbc_mac(with, value.block_0, value.associated_data, value.payload);
        if (!mac.has_value())
            return mac;
        if (const auto encrypted = ctr(with, value.counter_0, *mac, value.payload); !encrypted.has_value())
            return std::unexpected(encrypted.error());
        return mac;
    }

    expected_void_t open(const cipher& with, const message& value, const block_t& mac) noexcept
    {
        auto received_mac = mac;
        if (const auto decrypted = ctr(with, value.counter_0, received_mac, value.payload); !decrypted.has_value())
            return decrypted;

        const auto expected_mac = cbc_mac(with, value.block_0, value.associated_data, value.payload);
        if (!expected_mac.has_value())
        {
            cleanse(value.payload);
            return std::unexpected(expected_mac.error());
        }

        // One verdict for every altered field - header, session id, sequence, serial, tag, payload or MAC - so
        // a caller learns that the frame is not authentic and nothing about why.
        if (!constant_time_equal(received_mac, *expected_mac))
        {
            cleanse(value.payload);
            return std::unexpected(make_error_code(error::secure_authentication_failed));
        }

        return {};
    }

    block_t wrapper_block_0(const sequence_information_t& sequence, const serial_number_t& serial_number, const message_tag_t& message_tag,
                            const std::uint16_t payload_length) noexcept
    {
        block_t result {};
        auto cursor = std::copy(sequence.begin(), sequence.end(), result.begin());
        cursor = std::copy(serial_number.begin(), serial_number.end(), cursor);
        cursor = std::copy(message_tag.begin(), message_tag.end(), cursor);
        *cursor++ = static_cast<std::uint8_t>(payload_length >> 8u);
        *cursor = static_cast<std::uint8_t>(payload_length & 0xFFu);
        return result;
    }

    block_t wrapper_counter_0(const sequence_information_t& sequence, const serial_number_t& serial_number,
                              const message_tag_t& message_tag) noexcept
    {
        auto result = wrapper_block_0(sequence, serial_number, message_tag, 0u);
        result[14u] = 0xFFu;
        result[15u] = 0x00u;
        return result;
    }

    secret_key_result_t derive_session_key(const crypto_backend& backend, const x25519_private_key& private_key,
                                           const x25519_public_key_t& peer_public_key) noexcept
    {
        secret_bytes<x25519_key_size> shared {};
        if (!backend.x25519_derive(private_key.bytes(), peer_public_key, shared.mutable_bytes()))
            return std::unexpected(backend_failure());

        secret_bytes<sha256_size> digest {};
        if (!backend.sha256(shared.bytes(), digest.mutable_bytes()))
            return std::unexpected(backend_failure());
        return secret_key {digest.bytes().first<key_size>()};
    }

    secret_key_result_t derive_password_key(const crypto_backend& backend, const std::string_view password,
                                            const std::string_view salt) noexcept
    {
        secret_key key {};
        if (!backend.pbkdf2_sha256(as_octets(password), as_octets(salt), password_iterations, key.mutable_bytes()))
            return std::unexpected(backend_failure());
        return key;
    }
}
