/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/detail/keyring_format.hpp>

#include <kmx/aio/knx/error.hpp>

#include <algorithm>
#include <array>
#include <vector>

namespace kmx::aio::knx::secure::detail
{
    /// @brief Octets of random prefix ahead of every encrypted keyring password.
    static constexpr std::size_t password_prefix_size = 8u;
    /// @brief Longest string the signature stream's one-octet length prefix can describe.
    static constexpr std::size_t longest_signed_string = 0xFFu;

    [[nodiscard]] static std::unexpected<std::error_code> malformed() noexcept
    {
        return std::unexpected(make_error_code(error::malformed_frame));
    }

    [[nodiscard]] static constexpr int base64_value(const char value) noexcept
    {
        if ((value >= 'A') && (value <= 'Z'))
            return value - 'A';
        if ((value >= 'a') && (value <= 'z'))
            return value - 'a' + 26;
        if ((value >= '0') && (value <= '9'))
            return value - '0' + 52;
        if (value == '+')
            return 62;
        if (value == '/')
            return 63;
        return -1;
    }

    /// @brief Decodes one four-character quantum, of which the last @p padding characters must be `=`.
    [[nodiscard]] static bool decode_quantum(const std::string_view quantum, const std::size_t padding,
                                             byte_buffer_t& output) noexcept(false)
    {
        std::uint32_t bits {};
        for (std::size_t index {}; index < quantum.size(); ++index)
        {
            const auto padded = index >= (quantum.size() - padding);
            const auto value = padded ? ((quantum[index] == '=') ? 0 : -1) : base64_value(quantum[index]);
            if (value < 0)
                return false;
            bits = (bits << 6u) | static_cast<std::uint32_t>(value);
        }
        if (((padding == 1u) && ((bits & 0xFFu) != 0u)) || ((padding == 2u) && ((bits & 0xFFFFu) != 0u)))
            return false;
        output.push_back(static_cast<std::uint8_t>(bits >> 16u));
        if (padding < 2u)
            output.push_back(static_cast<std::uint8_t>((bits >> 8u) & 0xFFu));
        if (padding < 1u)
            output.push_back(static_cast<std::uint8_t>(bits & 0xFFu));
        return true;
    }

    octets_result_t base64_decode(const std::string_view text) noexcept(false)
    {
        if (text.empty() || ((text.size() % 4u) != 0u))
            return malformed();
        const std::size_t padding = text.ends_with("==") ? 2u : (text.ends_with('=') ? 1u : 0u);

        byte_buffer_t result {};
        result.reserve((text.size() / 4u) * 3u);
        for (std::size_t offset {}; offset < text.size(); offset += 4u)
        {
            const auto last = (offset + 4u) == text.size();
            if (!decode_quantum(text.substr(offset, 4u), last ? padding : 0u, result))
                return malformed();
        }
        return result;
    }

    std::string base64_encode(const cspan_uint8_t octets) noexcept(false)
    {
        static constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result {};
        result.reserve(((octets.size() + 2u) / 3u) * 4u);
        for (std::size_t offset {}; offset < octets.size(); offset += 3u)
        {
            const auto available = std::min<std::size_t>(3u, octets.size() - offset);
            std::uint32_t bits = static_cast<std::uint32_t>(octets[offset]) << 16u;
            if (available > 1u)
                bits |= static_cast<std::uint32_t>(octets[offset + 1u]) << 8u;
            if (available > 2u)
                bits |= octets[offset + 2u];
            for (std::size_t index {}; index < 4u; ++index)
                result.push_back((index <= available) ? alphabet[(bits >> (18u - (6u * index))) & 0x3Fu] : '=');
        }
        return result;
    }

    /// @brief Appends one length-prefixed string to the signature stream.
    [[nodiscard]] static bool append_signed_string(byte_buffer_t& stream, const std::string_view text) noexcept(false)
    {
        if (text.size() > longest_signed_string)
            return false;
        stream.push_back(static_cast<std::uint8_t>(text.size()));
        stream.insert(stream.end(), text.begin(), text.end());
        return true;
    }

    /// @brief Appends a start element: `01`, the name, then the signed attributes sorted by name.
    [[nodiscard]] static bool append_signed_start(byte_buffer_t& stream, const xml_event& event) noexcept(false)
    {
        stream.push_back(0x01u);
        if (!append_signed_string(stream, event.name))
            return false;

        std::vector<const xml_attribute*> signed_attributes {};
        for (const auto& attribute: event.attributes)
            if ((attribute.name != "xmlns") && (attribute.name != "Signature"))
                signed_attributes.push_back(&attribute);
        std::ranges::sort(signed_attributes,
                          [](const xml_attribute* lhs, const xml_attribute* rhs) noexcept { return lhs->name < rhs->name; });

        return std::ranges::all_of(
            signed_attributes, [&stream](const xml_attribute* attribute)
            { return append_signed_string(stream, attribute->name) && append_signed_string(stream, attribute->value); });
    }

    octets_result_t keyring_signature_stream(const xml_events_t& events, const secret_key& password_hash) noexcept(false)
    {
        byte_buffer_t stream {};
        for (const auto& event: events)
        {
            if (event.kind == xml_event_kind::end)
                stream.push_back(0x02u);
            else if (!append_signed_start(stream, event))
                return malformed();
        }
        auto encoded_hash = base64_encode(password_hash.bytes());
        const auto appended = append_signed_string(stream, encoded_hash);
        cleanse(span_uint8_t {reinterpret_cast<std::uint8_t*>(encoded_hash.data()), encoded_hash.size()});
        if (!appended)
            return malformed();
        return stream;
    }

    expected_void_t verify_keyring_signature(const crypto_backend& backend, const xml_events_t& events,
                                             const secret_key& password_hash) noexcept(false)
    {
        const auto* const signature_text = events.empty() ? nullptr : find_attribute(events.front(), "Signature");
        if (signature_text == nullptr)
            return malformed();
        const auto signature = base64_decode(*signature_text);
        auto stream = keyring_signature_stream(events, password_hash);
        if (!signature.has_value() || !stream.has_value() || (signature->size() != key_size))
            return malformed();

        std::array<std::uint8_t, sha256_size> digest {};
        const auto hashed = backend.sha256(*stream, digest);
        cleanse(*stream);
        if (!hashed)
            return std::unexpected(make_error_code(error::crypto_failure));
        if (!constant_time_equal(cspan_uint8_t {digest.data(), key_size}, *signature))
            return std::unexpected(make_error_code(error::keyring_signature_invalid));
        return {};
    }

    mac_result_t keyring_initialisation_vector(const crypto_backend& backend, const std::string_view created) noexcept
    {
        std::array<std::uint8_t, sha256_size> digest {};
        if (!backend.sha256(cspan_uint8_t {reinterpret_cast<const std::uint8_t*>(created.data()), created.size()}, digest))
            return std::unexpected(make_error_code(error::crypto_failure));
        block_t iv {};
        std::copy_n(digest.begin(), aes_block_size, iv.begin());
        return iv;
    }

    secret_key_result_t decrypt_keyring_key(const crypto_backend& backend, const std::string_view encoded, const secret_key& password_hash,
                                            const block_t& iv) noexcept(false)
    {
        const auto cipher = base64_decode(encoded);
        if (!cipher.has_value() || (cipher->size() != key_size))
            return malformed();
        secret_key key {};
        if (!backend.cbc_decrypt(password_hash.bytes(), iv, *cipher, key.mutable_bytes()))
            return std::unexpected(make_error_code(error::crypto_failure));
        return key;
    }

    secret_string_result_t decrypt_keyring_password(const crypto_backend& backend, const std::string_view encoded,
                                                    const secret_key& password_hash, const block_t& iv) noexcept(false)
    {
        const auto cipher = base64_decode(encoded);
        if (!cipher.has_value() || ((cipher->size() % aes_block_size) != 0u))
            return malformed();
        byte_buffer_t plain(cipher->size(), 0u);
        if (!backend.cbc_decrypt(password_hash.bytes(), iv, *cipher, plain))
        {
            cleanse(plain);
            return std::unexpected(make_error_code(error::crypto_failure));
        }

        const std::size_t padding = plain.back();
        // ETS 5.7.5 and earlier pad to two blocks, so the count is bounded by what follows the prefix alone.
        const auto usable = (padding != 0u) && ((password_prefix_size + padding) <= plain.size());
        auto password = usable ? secret_string {std::string_view {reinterpret_cast<const char*>(plain.data() + password_prefix_size),
                                                                  plain.size() - password_prefix_size - padding}} :
                                 secret_string {};
        cleanse(plain);
        if (!usable)
            return malformed();
        return password;
    }
}
