/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/keyring.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kmx::aio::test::knx::keyring_conformance_test
{
    namespace internal
    {
        struct case_definition
        {
            std::string case_id {};
            std::string keyring_file {};
            std::string device_id {};
            std::string key_id {};
            bool use_decryptor {};
            std::optional<std::array<std::uint8_t, kmx::aio::knx::keyring::key_size>> expected_key {};
            std::optional<kmx::aio::knx::error> expected_error {};
        };

        [[nodiscard]] std::string trim(const std::string_view value)
        {
            std::size_t begin {};
            std::size_t end = value.size();
            while ((begin < end) && (std::isspace(static_cast<unsigned char>(value[begin])) != 0))
                ++begin;
            while ((end > begin) && (std::isspace(static_cast<unsigned char>(value[end - 1u])) != 0))
                --end;
            return std::string(value.substr(begin, end - begin));
        }

        [[nodiscard]] std::vector<std::string> split_tsv(const std::string_view line)
        {
            std::vector<std::string> fields {};
            std::size_t start {};
            while (start <= line.size())
            {
                const auto tab = line.find('\t', start);
                if (tab == std::string_view::npos)
                {
                    fields.emplace_back(line.substr(start));
                    break;
                }

                fields.emplace_back(line.substr(start, tab - start));
                start = tab + 1u;
            }
            return fields;
        }

        [[nodiscard]] int hex_value(const char value) noexcept
        {
            if ((value >= '0') && (value <= '9'))
                return value - '0';
            if ((value >= 'a') && (value <= 'f'))
                return value - 'a' + 10;
            if ((value >= 'A') && (value <= 'F'))
                return value - 'A' + 10;
            return -1;
        }

        [[nodiscard]] std::optional<std::vector<std::uint8_t>> decode_hex(std::string text)
        {
            text = trim(text);
            text.erase(
                std::remove_if(
                    text.begin(),
                    text.end(),
                    [](const char ch) {
                        return (ch == ' ') || (ch == '\t') || (ch == '\n') || (ch == '\r') || (ch == '_');
                    }),
                text.end());

            if ((text.size() % 2u) != 0u)
                return std::nullopt;

            std::vector<std::uint8_t> bytes(text.size() / 2u, 0u);
            for (std::size_t index = 0u; index < bytes.size(); ++index)
            {
                const auto high = hex_value(text[index * 2u]);
                const auto low = hex_value(text[index * 2u + 1u]);
                if ((high < 0) || (low < 0))
                    return std::nullopt;
                bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
            }

            return bytes;
        }

        [[nodiscard]] std::optional<std::array<std::uint8_t, kmx::aio::knx::keyring::key_size>> decode_hex_key(
            const std::string_view text)
        {
            const auto decoded = decode_hex(std::string(text));
            if (!decoded.has_value())
                return std::nullopt;
            if (decoded->size() != kmx::aio::knx::keyring::key_size)
                return std::nullopt;

            std::array<std::uint8_t, kmx::aio::knx::keyring::key_size> result {};
            for (std::size_t index = 0u; index < result.size(); ++index)
                result[index] = (*decoded)[index];
            return result;
        }

        [[nodiscard]] std::optional<kmx::aio::knx::error> parse_error(const std::string_view text)
        {
            const auto value = trim(text);
            if (value.empty())
                return std::nullopt;
            if (value == "malformed_frame")
                return kmx::aio::knx::error::malformed_frame;
            if (value == "secure_unsupported")
                return kmx::aio::knx::error::secure_unsupported;
            if (value == "invalid_length")
                return kmx::aio::knx::error::invalid_length;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<bool> parse_bool(const std::string_view text)
        {
            const auto value = trim(text);
            if ((value == "true") || (value == "1"))
                return true;
            if ((value == "false") || (value == "0"))
                return false;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::unordered_map<std::string, std::uint8_t>> parse_password_masks(
            const std::filesystem::path& path)
        {
            std::ifstream input(path);
            if (!input.is_open())
                return std::nullopt;

            std::unordered_map<std::string, std::uint8_t> masks {};
            std::string line {};
            while (std::getline(input, line))
            {
                if (!line.empty() && (line.back() == '\r'))
                    line.pop_back();

                const auto trimmed = trim(line);
                if (trimmed.empty() || trimmed.starts_with('#') || trimmed.starts_with("password_id\t"))
                    continue;

                const auto fields = split_tsv(line);
                if (fields.size() != 2u)
                    return std::nullopt;

                const auto key = trim(fields[0u]);
                const auto decoded = decode_hex(trim(fields[1u]));
                if (key.empty() || !decoded.has_value() || (decoded->size() != 1u))
                    return std::nullopt;

                masks[key] = (*decoded)[0u];
            }

            return masks;
        }

        [[nodiscard]] std::optional<std::vector<case_definition>> parse_cases(const std::filesystem::path& path)
        {
            std::ifstream input(path);
            if (!input.is_open())
                return std::nullopt;

            std::vector<case_definition> cases {};
            std::string line {};
            while (std::getline(input, line))
            {
                if (!line.empty() && (line.back() == '\r'))
                    line.pop_back();

                const auto trimmed = trim(line);
                if (trimmed.empty() || trimmed.starts_with('#') || trimmed.starts_with("case_id\t"))
                    continue;

                auto fields = split_tsv(line);
                if (fields.size() == 6u)
                    fields.emplace_back("");
                if (fields.size() != 7u)
                    return std::nullopt;

                case_definition value {};
                value.case_id = trim(fields[0u]);
                value.keyring_file = trim(fields[1u]);
                value.device_id = trim(fields[2u]);
                value.key_id = trim(fields[3u]);

                const auto use_decryptor = parse_bool(fields[4u]);
                if (!use_decryptor.has_value())
                    return std::nullopt;
                value.use_decryptor = *use_decryptor;

                const auto expected_key = trim(fields[5u]);
                if (!expected_key.empty())
                {
                    value.expected_key = decode_hex_key(expected_key);
                    if (!value.expected_key.has_value())
                        return std::nullopt;
                }

                const auto expected_error = trim(fields[6u]);
                if (!expected_error.empty())
                {
                    value.expected_error = parse_error(expected_error);
                    if (!value.expected_error.has_value())
                        return std::nullopt;
                }

                cases.push_back(std::move(value));
            }

            if (cases.empty())
                return std::nullopt;

            return cases;
        }

        [[nodiscard]] std::optional<std::string> read_text_file(const std::filesystem::path& path)
        {
            std::ifstream input(path);
            if (!input.is_open())
                return std::nullopt;
            std::string data {
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>(),
            };
            return data;
        }

        class xor_mask_decryptor final: public kmx::aio::knx::keyring::decryptor
        {
        public:
            explicit xor_mask_decryptor(std::unordered_map<std::string, std::uint8_t> masks) noexcept:
                masks_(std::move(masks)) {}

            [[nodiscard]] std::expected<std::array<std::uint8_t, kmx::aio::knx::keyring::key_size>, kmx::aio::knx::error>
            decrypt_key(
                const std::span<const std::uint8_t> encrypted_key,
                const std::string_view password_id) noexcept override
            {
                if (encrypted_key.size() != kmx::aio::knx::keyring::key_size)
                    return std::unexpected(kmx::aio::knx::error::malformed_frame);

                const auto it = masks_.find(std::string(password_id));
                if (it == masks_.end())
                    return std::unexpected(kmx::aio::knx::error::secure_unsupported);

                std::array<std::uint8_t, kmx::aio::knx::keyring::key_size> key {};
                for (std::size_t index = 0u; index < key.size(); ++index)
                    key[index] = encrypted_key[index] ^ it->second;
                return key;
            }

        private:
            std::unordered_map<std::string, std::uint8_t> masks_ {};
        };
    } // namespace internal

    TEST_CASE("knx keyring conformance vectors", "[knx][keyring][conformance]")
    {
        const auto* env_dir = std::getenv("KMX_KNX_KEYRING_CONFORMANCE_DIR");
        const std::filesystem::path conformance_dir {
            (env_dir != nullptr) && (env_dir[0] != '\0')
                ? env_dir
                : "documentation/features/knx/conformance/keyrings"};

        if (!std::filesystem::exists(conformance_dir))
            SKIP("keyring conformance directory is not present");

        const auto* env_cases = std::getenv("KMX_KNX_KEYRING_CASES_FILE");
        const auto* env_passwords = std::getenv("KMX_KNX_KEYRING_PASSWORDS_FILE");
        const auto cases_path = std::filesystem::path {
            (env_cases != nullptr) && (env_cases[0] != '\0')
                ? env_cases
                : (conformance_dir / "cases.tsv")};
        const auto password_path = std::filesystem::path {
            (env_passwords != nullptr) && (env_passwords[0] != '\0')
                ? env_passwords
                : (conformance_dir / "passwords.tsv")};

        const auto cases = internal::parse_cases(cases_path);
        const auto password_masks = internal::parse_password_masks(password_path);
        REQUIRE(cases.has_value());
        REQUIRE(password_masks.has_value());

        internal::xor_mask_decryptor decryptor {*password_masks};
        for (const auto& test_case: *cases)
        {
            INFO("keyring_case=" << test_case.case_id);

            const auto keyring_file = conformance_dir / test_case.keyring_file;
            const auto document = internal::read_text_file(keyring_file);
            REQUIRE(document.has_value());

            auto* selected_decryptor = test_case.use_decryptor
                ? static_cast<kmx::aio::knx::keyring::decryptor*>(&decryptor)
                : nullptr;

            const auto parsed = kmx::aio::knx::keyring::parse_selected(
                *document,
                test_case.device_id,
                test_case.key_id,
                selected_decryptor);

            if (test_case.expected_error.has_value())
            {
                REQUIRE(!parsed.has_value());
                CHECK(parsed.error() == *test_case.expected_error);
                continue;
            }

            REQUIRE(parsed.has_value());
            REQUIRE(test_case.expected_key.has_value());
            CHECK(parsed->device_id == test_case.device_id);
            CHECK(parsed->key == *test_case.expected_key);
        }
    }
}
