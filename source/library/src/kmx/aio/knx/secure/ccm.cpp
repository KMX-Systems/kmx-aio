/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/detail/ccm.hpp>

#include <kmx/aio/knx/error.hpp>

#include <algorithm>

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

    mac_result_t cbc_mac(const crypto_backend& backend, const secret_key& key, const block_t& block_0, const cspan_uint8_t associated_data,
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
        const auto computed = backend.cbc_mac(key.bytes(), cspan_uint8_t {input.data(), padded}, mac);
        cleanse(span_uint8_t {input.data(), padded});
        if (!computed)
            return std::unexpected(backend_failure());
        return mac;
    }

    expected_void_t ctr(const crypto_backend& backend, const secret_key& key, const block_t& counter_0, const span_uint8_t mac,
                        const span_uint8_t payload) noexcept
    {
        if (!backend.ctr(key.bytes(), counter_0, mac, payload))
            return std::unexpected(backend_failure());
        return {};
    }

    mac_result_t seal(const crypto_backend& backend, const secret_key& key, const block_t& block_0, const block_t& counter_0,
                      const cspan_uint8_t associated_data, const span_uint8_t payload) noexcept
    {
        auto mac = cbc_mac(backend, key, block_0, associated_data, payload);
        if (!mac.has_value())
            return mac;
        if (const auto encrypted = ctr(backend, key, counter_0, *mac, payload); !encrypted.has_value())
            return std::unexpected(encrypted.error());
        return mac;
    }

    expected_void_t open(const crypto_backend& backend, const secret_key& key, const block_t& block_0, const block_t& counter_0,
                         const cspan_uint8_t associated_data, const span_uint8_t payload, const block_t& mac) noexcept
    {
        auto received_mac = mac;
        if (const auto decrypted = ctr(backend, key, counter_0, received_mac, payload); !decrypted.has_value())
            return decrypted;

        const auto expected_mac = cbc_mac(backend, key, block_0, associated_data, payload);
        if (!expected_mac.has_value())
        {
            cleanse(payload);
            return std::unexpected(expected_mac.error());
        }
        // One verdict for every altered field - header, session id, sequence, serial, tag, payload or MAC - so
        // a caller learns that the frame is not authentic and nothing about why.
        if (!constant_time_equal(received_mac, *expected_mac))
        {
            cleanse(payload);
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
