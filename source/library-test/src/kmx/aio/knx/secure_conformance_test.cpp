/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/secure.hpp>

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
#include <vector>

namespace kmx::aio::test::knx::secure_conformance_test
{
    using namespace kmx::aio::knx;

    namespace internal
    {
        struct conformance_case
        {
            std::string case_id {};
            secure::profile selected = secure::profile::none;
            std::uint64_t sequence {};
            std::vector<std::uint8_t> payload {};
            std::vector<std::uint8_t> wire {};
            std::optional<error> expected_error {};
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
                        return (ch == ' ') || (ch == '\t') || (ch == '_') || (ch == '\n') || (ch == '\r');
                    }),
                text.end());

            if (text.empty())
                return std::vector<std::uint8_t> {};
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

        [[nodiscard]] std::optional<std::uint64_t> parse_u64(const std::string_view text)
        {
            const auto value = trim(text);
            if (value.empty())
                return std::nullopt;

            try
            {
                std::size_t consumed {};
                const auto parsed = std::stoull(value, &consumed, 0);
                if (consumed != value.size())
                    return std::nullopt;
                return static_cast<std::uint64_t>(parsed);
            }
            catch (...) // NOLINT(bugprone-empty-catch)
            {
                return std::nullopt;
            }
        }

        [[nodiscard]] std::optional<secure::profile> parse_profile(const std::string_view text)
        {
            const auto value = trim(text);
            if (value == "ip_secure")
                return secure::profile::ip_secure;
            if (value == "data_secure")
                return secure::profile::data_secure;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<error> parse_error(const std::string_view text)
        {
            const auto value = trim(text);
            if (value.empty())
                return std::nullopt;
            if (value == "malformed_frame")
                return error::malformed_frame;
            if (value == "unsupported_service")
                return error::unsupported_service;
            if (value == "invalid_configuration")
                return error::invalid_configuration;
            if (value == "invalid_length")
                return error::invalid_length;
            if (value == "sequence_error")
                return error::sequence_error;
            return std::nullopt;
        }

        [[nodiscard]] std::expected<std::vector<conformance_case>, std::string> parse_cases(
            const std::filesystem::path& path)
        {
            std::ifstream input(path);
            if (!input.is_open())
                return std::unexpected("could not open secure conformance file");

            std::vector<conformance_case> cases {};
            std::string line {};
            std::size_t line_number {};
            while (std::getline(input, line))
            {
                ++line_number;
                if (!line.empty() && (line.back() == '\r'))
                    line.pop_back();

                const auto trimmed = trim(line);
                if (trimmed.empty() || trimmed.starts_with('#'))
                    continue;
                if (trimmed.starts_with("case_id\t"))
                    continue;

                auto fields = split_tsv(line);
                if (fields.size() == 5u)
                    fields.emplace_back("");
                if (fields.size() != 6u)
                    return std::unexpected("invalid field count at secure conformance line " + std::to_string(line_number));

                conformance_case value {};
                value.case_id = trim(fields[0u]);
                const auto expected = parse_error(fields[5u]);
                if (!trim(fields[5u]).empty() && !expected.has_value())
                    return std::unexpected("unknown expected error at line " + std::to_string(line_number));
                value.expected_error = expected;

                const auto wire = decode_hex(fields[4u]);
                if (!wire.has_value())
                    return std::unexpected("invalid wire hex at line " + std::to_string(line_number));
                value.wire = *wire;

                if (!value.expected_error.has_value())
                {
                    const auto selected = parse_profile(fields[1u]);
                    const auto sequence = parse_u64(fields[2u]);
                    const auto payload = decode_hex(fields[3u]);
                    if (!selected.has_value() || !sequence.has_value() || !payload.has_value())
                        return std::unexpected("invalid positive vector fields at line " + std::to_string(line_number));

                    value.selected = *selected;
                    value.sequence = *sequence;
                    value.payload = *payload;
                }

                cases.push_back(std::move(value));
            }

            if (cases.empty())
                return std::unexpected("secure conformance file does not contain any cases");

            return cases;
        }
    } // namespace internal

    TEST_CASE("knx secure profile conformance vectors", "[knx][secure][conformance]")
    {
        const auto* env_path = std::getenv("KMX_KNX_SECURE_VECTOR_FILE");
        const auto vector_path = std::filesystem::path {
            (env_path != nullptr) && (env_path[0] != '\0')
                ? env_path
                : "documentation/features/knx/conformance/secure-profile-vectors.tsv"};

        if (!std::filesystem::exists(vector_path))
            SKIP("secure conformance vector file is not present");

        const auto parsed = internal::parse_cases(vector_path);
        REQUIRE(parsed.has_value());

        for (const auto& test_case: *parsed)
        {
            INFO("secure_case=" << test_case.case_id);

            if (test_case.expected_error.has_value())
            {
                const auto decoded = secure::decode_secure_packet(test_case.wire);
                REQUIRE(!decoded.has_value());
                CHECK(decoded.error() == make_error_code(*test_case.expected_error));
                continue;
            }

            std::vector<std::uint8_t> encoded(test_case.wire.size(), 0u);
            const secure::packet packet {
                .selected = test_case.selected,
                .sequence = test_case.sequence,
                .payload = test_case.payload,
            };

            REQUIRE(secure::encode_secure_packet(encoded, packet).has_value());
            CHECK(encoded == test_case.wire);

            const auto decoded = secure::decode_secure_packet(test_case.wire);
            REQUIRE(decoded.has_value());
            CHECK(decoded->selected == test_case.selected);
            CHECK(decoded->sequence == test_case.sequence);
            CHECK(decoded->payload == test_case.payload);
        }
    }
}
