/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/keyring.hpp>

#include <optional>

#include <vector>

namespace kmx::aio::knx::keyring
{
    [[nodiscard]] static constexpr int hex_value(const char value) noexcept
    {
        if ((value >= '0') && (value <= '9'))
            return value - '0';
        if ((value >= 'a') && (value <= 'f'))
            return value - 'a' + 10;
        if ((value >= 'A') && (value <= 'F'))
            return value - 'A' + 10;
        return -1;
    }

    /// @brief Indicates whether a character is XML whitespace.
    [[nodiscard]] static constexpr bool is_xml_space(const char value) noexcept
    {
        return (value == ' ') || (value == '\t') || (value == '\n') || (value == '\r');
    }

    /// @brief Returns the offset of the first non-whitespace character at or after @p cursor.
    [[nodiscard]] static constexpr std::size_t skip_xml_spaces(const std::string_view text, std::size_t cursor) noexcept
    {
        while ((cursor < text.size()) && is_xml_space(text[cursor]))
            ++cursor;
        return cursor;
    }

    /// @brief Indicates whether an attribute name starting at @p offset begins a name rather than ending one.
    /// @details Without this, a search for "key" would also match the tail of "encrypted-key".
    [[nodiscard]] static constexpr bool starts_attribute(const std::string_view element, const std::size_t offset) noexcept
    {
        return (offset == 0u) || (element[offset - 1u] == '<') || is_xml_space(element[offset - 1u]);
    }

    [[nodiscard]] static std::string_view attribute_value(
        const std::string_view element, const std::string_view name) noexcept
    {
        std::size_t search_offset {};
        while (true)
        {
            const auto name_offset = element.find(name, search_offset);
            if (name_offset == std::string_view::npos)
                return {};

            auto cursor = skip_xml_spaces(element, name_offset + name.size());
            const bool valid_suffix = (cursor < element.size()) && (element[cursor] == '=');
            if (!starts_attribute(element, name_offset) || !valid_suffix)
            {
                search_offset = name_offset + 1u;
                continue;
            }

            cursor = skip_xml_spaces(element, cursor + 1u);

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

    [[nodiscard]] static key_result_t decode_hex_key(const std::string_view key_text) noexcept
    {
        if (key_text.size() != key_size * 2u)
            return std::unexpected(error::malformed_frame);

        key_t result {};
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

    /// @brief A decoded hex blob, or the error explaining why the text was not valid hex.
    using blob_result_t = std::expected<byte_buffer_t, error>;

    [[nodiscard]] static blob_result_t decode_hex_blob(const std::string_view text) noexcept
    {
        if (text.empty() || ((text.size() % 2u) != 0u))
            return std::unexpected(error::malformed_frame);

        byte_buffer_t result(text.size() / 2u, 0u);
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

    key_record_result_t parse(const std::string_view document) noexcept
    {
        return parse_selected(document, {}, {}, nullptr);
    }

    /// @brief A key read from one element, nothing when this element is not the one to use.
    using optional_key_record_result_t = std::optional<key_record_result_t>;

    /// @brief Rejects a document that is not a keyring, or that could expand an XML entity.
    /// @details A DOCTYPE or ENTITY declaration is an expansion attack surface a keyring never needs.
    [[nodiscard]] static bool safe_keyring_document(const std::string_view document) noexcept
    {
        return (document.find("<!DOCTYPE") == std::string_view::npos) &&
               (document.find("<!ENTITY") == std::string_view::npos) && (document.find("<Key") != std::string_view::npos);
    }

    /// @brief Finds the next `<Key>` element at or after @p cursor, advancing it past what was found.
    /// @param document The keyring document.
    /// @param cursor Where to search from; left past the element, or at the end when there is none.
    /// @return The element including its brackets, an empty view when there is none, or why one is malformed.
    [[nodiscard]] static std::expected<std::string_view, error> next_key_element(const std::string_view document,
                                                                                 std::size_t& cursor) noexcept
    {
        const auto start = document.find("<Key", cursor);
        if (start == std::string_view::npos)
        {
            cursor = document.size();
            return std::string_view {};
        }

        const auto end = document.find('>', start);
        if (end == std::string_view::npos)
            return std::unexpected(error::malformed_frame);
        // A '<' before the '>' means the element never closed, which a keyring's flat elements never do.
        const auto nested_tag = document.find('<', start + 1u);
        if ((nested_tag != std::string_view::npos) && (nested_tag < end))
            return std::unexpected(error::malformed_frame);

        cursor = end + 1u;
        return document.substr(start, end - start + 1u);
    }

    /// @brief Indicates whether an element is the one the caller asked for; an empty filter matches all.
    [[nodiscard]] static bool element_selected(const std::string_view element_device_id, const std::string_view element_key_id,
                                                const std::string_view device_id, const std::string_view key_id) noexcept
    {
        return (device_id.empty() || (element_device_id == device_id)) && (key_id.empty() || (element_key_id == key_id));
    }

    /// @brief Decrypts the encrypted key an element carries.
    /// @param element The element, for the password id beside the key.
    /// @param encrypted_key The encrypted key's hex text.
    /// @param element_device_id The device id to carry into the result.
    /// @param key_decryptor The decryptor to use; never null here.
    /// @return The record, or why it could not be produced.
    [[nodiscard]] static key_record_result_t decrypt_element_key(const std::string_view element,
                                                                 const std::string_view encrypted_key,
                                                                 const std::string_view element_device_id,
                                                                 decryptor& key_decryptor) noexcept
    {
        const auto encoded_blob = decode_hex_blob(encrypted_key);
        if (!encoded_blob.has_value())
            return std::unexpected(encoded_blob.error());

        const auto password_id = attribute_value(element, "password-id");
        const auto decrypted = key_decryptor.decrypt_key(encoded_blob.value(), password_id);
        if (!decrypted.has_value())
            return std::unexpected(decrypted.error());

        key_record result {};
        result.key = *decrypted;
        result.device_id = element_device_id;
        return result;
    }

    /// @brief Reads the key one element carries, in the clear or encrypted.
    /// @param element The element to read.
    /// @param element_device_id The device id to carry into the result.
    /// @param key_decryptor The decryptor for an encrypted key, or null when none is available.
    /// @param matched_encrypted Set when an encrypted key was seen that could not be decrypted here.
    /// @return The record, nothing when the search should go on, or why the element could not be read.
    [[nodiscard]] static optional_key_record_result_t key_from_element(const std::string_view element,
                                                                       const std::string_view element_device_id,
                                                                       decryptor* const key_decryptor,
                                                                       bool& matched_encrypted) noexcept
    {
        if (const auto clear_key = attribute_value(element, "key"); !clear_key.empty())
        {
            const auto decoded_key = decode_hex_key(clear_key);
            if (!decoded_key.has_value())
                return key_record_result_t {std::unexpected(decoded_key.error())};

            key_record result {};
            result.key = *decoded_key;
            result.device_id = element_device_id;
            return key_record_result_t {result};
        }

        const auto encrypted_key = attribute_value(element, "encrypted-key");
        if (encrypted_key.empty())
            return key_record_result_t {std::unexpected(error::malformed_frame)};

        matched_encrypted = true;
        // Left for a later element rather than failed on: another may carry its key in the clear.
        if (key_decryptor == nullptr)
            return {};
        return decrypt_element_key(element, encrypted_key, element_device_id, *key_decryptor);
    }

    key_record_result_t parse_selected(
        const std::string_view document,
        const std::string_view device_id,
        const std::string_view key_id,
        decryptor* const key_decryptor) noexcept
    {
        if (document.empty() || (document.size() > max_document_size))
            return std::unexpected(error::invalid_length);
        if (!safe_keyring_document(document))
            return std::unexpected(error::malformed_frame);

        bool matched_encrypted {};
        std::size_t cursor {};
        while (true)
        {
            const auto element = next_key_element(document, cursor);
            if (!element.has_value())
                return std::unexpected(element.error());
            if (element->empty())
                break;

            const auto element_device_id = attribute_value(*element, "device-id");
            if (!element_selected(element_device_id, attribute_value(*element, "key-id"), device_id, key_id))
                continue;
            if (auto found = key_from_element(*element, element_device_id, key_decryptor, matched_encrypted); found.has_value())
                return *found;
        }

        // A keyring whose only match was encrypted names the missing decryptor, not a malformed document.
        if (matched_encrypted && (key_decryptor == nullptr))
            return std::unexpected(error::secure_unsupported);
        return std::unexpected(error::malformed_frame);
    }
}
