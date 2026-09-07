/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/keyring.hpp>

#include <vector>

namespace kmx::aio::knx::keyring
{
    namespace
    {
        [[nodiscard]] constexpr int hex_value(const char value) noexcept
        {
            if ((value >= '0') && (value <= '9'))
                return value - '0';
            if ((value >= 'a') && (value <= 'f'))
                return value - 'a' + 10;
            if ((value >= 'A') && (value <= 'F'))
                return value - 'A' + 10;
            return -1;
        }

        [[nodiscard]] std::string_view attribute_value(
            const std::string_view element, const std::string_view name) noexcept
        {
            std::size_t search_offset = 0u;
            while (true)
            {
                const auto name_offset = element.find(name, search_offset);
                if (name_offset == std::string_view::npos)
                    return {};

                const bool valid_prefix =
                    (name_offset == 0u) ||
                    (element[name_offset - 1u] == '<') ||
                    (element[name_offset - 1u] == ' ') ||
                    (element[name_offset - 1u] == '\t') ||
                    (element[name_offset - 1u] == '\n') ||
                    (element[name_offset - 1u] == '\r');

                std::size_t cursor = name_offset + name.size();
                while (cursor < element.size() &&
                       ((element[cursor] == ' ') || (element[cursor] == '\t') ||
                        (element[cursor] == '\n') || (element[cursor] == '\r')))
                    ++cursor;

                const bool valid_suffix = (cursor < element.size()) && (element[cursor] == '=');
                if (!valid_prefix || !valid_suffix)
                {
                    search_offset = name_offset + 1u;
                    continue;
                }

                ++cursor;
                while (cursor < element.size() &&
                       ((element[cursor] == ' ') || (element[cursor] == '\t') ||
                        (element[cursor] == '\n') || (element[cursor] == '\r')))
                    ++cursor;

                if (cursor >= element.size())
                    return {};
                if ((element[cursor] != '"') && (element[cursor] != '\''))
                    return {};

                const auto quote = element[cursor];
                const auto value_start = cursor + 1u;
                const auto value_end = element.find(quote, value_start);
                if (value_end == std::string_view::npos)
                    return {};
                return element.substr(value_start, value_end - value_start);
            }
        }

        [[nodiscard]] std::expected<std::array<std::uint8_t, key_size>, error> decode_hex_key(
            const std::string_view key_text) noexcept
        {
            if (key_text.size() != key_size * 2u)
                return std::unexpected(error::malformed_frame);

            std::array<std::uint8_t, key_size> result {};
            for (std::size_t index = 0u; index < key_size; ++index)
            {
                const auto high = hex_value(key_text[index * 2u]);
                const auto low = hex_value(key_text[index * 2u + 1u]);
                if ((high < 0) || (low < 0))
                    return std::unexpected(error::malformed_frame);
                result[index] = static_cast<std::uint8_t>((high << 4) | low);
            }

            return result;
        }

        [[nodiscard]] std::expected<std::vector<std::uint8_t>, error> decode_hex_blob(
            const std::string_view text) noexcept
        {
            if (text.empty() || ((text.size() % 2u) != 0u))
                return std::unexpected(error::malformed_frame);

            std::vector<std::uint8_t> result(text.size() / 2u, 0u);
            for (std::size_t index = 0u; index < result.size(); ++index)
            {
                const auto high = hex_value(text[index * 2u]);
                const auto low = hex_value(text[index * 2u + 1u]);
                if ((high < 0) || (low < 0))
                    return std::unexpected(error::malformed_frame);
                result[index] = static_cast<std::uint8_t>((high << 4) | low);
            }

            return result;
        }
    }

    std::expected<key_record, error> parse(const std::string_view document) noexcept
    {
        return parse_selected(document, {}, {}, nullptr);
    }

    std::expected<key_record, error> parse_selected(
        const std::string_view document,
        const std::string_view device_id,
        const std::string_view key_id,
        decryptor* const key_decryptor) noexcept
    {
        if (document.empty() || document.size() > max_document_size)
            return std::unexpected(error::invalid_length);
        if ((document.find("<!DOCTYPE") != std::string_view::npos) ||
            (document.find("<!ENTITY") != std::string_view::npos) ||
            (document.find("<Key") == std::string_view::npos))
            return std::unexpected(error::malformed_frame);

        bool matched_encrypted = false;
        std::size_t cursor = 0u;
        while (true)
        {
            const auto start = document.find("<Key", cursor);
            if (start == std::string_view::npos)
                break;
            const auto end = document.find('>', start);
            if (end == std::string_view::npos)
                return std::unexpected(error::malformed_frame);
            const auto nested_tag = document.find('<', start + 1u);
            if ((nested_tag != std::string_view::npos) && (nested_tag < end))
                return std::unexpected(error::malformed_frame);

            const auto element = document.substr(start, end - start + 1u);
            cursor = end + 1u;

            const auto element_device_id = attribute_value(element, "device-id");
            if (!device_id.empty() && (element_device_id != device_id))
                continue;

            const auto element_key_id = attribute_value(element, "key-id");
            if (!key_id.empty() && (element_key_id != key_id))
                continue;

            const auto clear_key = attribute_value(element, "key");
            if (!clear_key.empty())
            {
                const auto decoded_key = decode_hex_key(clear_key);
                if (!decoded_key.has_value())
                    return std::unexpected(decoded_key.error());

                key_record result {};
                result.key = *decoded_key;
                result.device_id = element_device_id;
                return result;
            }

            const auto encrypted_key = attribute_value(element, "encrypted-key");
            if (!encrypted_key.empty())
            {
                matched_encrypted = true;
                if (key_decryptor == nullptr)
                    continue;

                const auto encoded_blob = decode_hex_blob(encrypted_key);
                if (!encoded_blob.has_value())
                    return std::unexpected(encoded_blob.error());

                const auto password_id = attribute_value(element, "password-id");
                const auto decrypted = key_decryptor->decrypt_key(encoded_blob.value(), password_id);
                if (!decrypted.has_value())
                    return std::unexpected(decrypted.error());

                key_record result {};
                result.key = *decrypted;
                result.device_id = element_device_id;
                return result;
            }

            return std::unexpected(error::malformed_frame);
        }

        if (matched_encrypted && key_decryptor == nullptr)
            return std::unexpected(error::secure_unsupported);

        return std::unexpected(error::malformed_frame);
    }
}
