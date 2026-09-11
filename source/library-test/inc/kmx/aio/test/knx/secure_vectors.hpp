/// @file inc/kmx/aio/test/knx/secure_vectors.hpp
/// @brief What the KNX Secure tests share: hexadecimal octets, keys, a failing backend, and the xknx vectors.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details The vectors are read from `documentation/features/knx/conformance/secure-routing-vectors.tsv`, which
///          `script/feature/knx/secure-vectors/generate.py` writes by driving xknx itself. The file is found from
///          this header's own path, so a test binary run from any working directory reads the same vectors.
#pragma once
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/secure/detail/crypto.hpp>
    #include <kmx/aio/knx/secure/key.hpp>
    #include <kmx/aio/knx/secure/secret_bytes.hpp>

    #include <algorithm>
    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <filesystem>
    #include <fstream>
    #include <string>
    #include <string_view>
    #include <vector>
#endif

namespace kmx::aio::test::knx::secure_vectors
{
    /// @brief Owned octets.
    using octets_t = std::vector<std::uint8_t>;

    /// @brief Reads hexadecimal text, ignoring spaces.
    [[nodiscard]] inline octets_t hex(const std::string_view text) noexcept(false)
    {
        std::string digits {};
        for (const auto character: text)
            if (character != ' ')
                digits.push_back(character);
        octets_t result {};
        for (std::size_t index {}; (index + 1u) < digits.size(); index += 2u)
            result.push_back(static_cast<std::uint8_t>(std::stoul(digits.substr(index, 2u), nullptr, 16)));
        return result;
    }

    /// @brief Copies octets into a fixed-size array; octets past the end are dropped and missing ones are zero.
    template <std::size_t Size>
    [[nodiscard]] std::array<std::uint8_t, Size> fixed(const cspan_uint8_t octets) noexcept
    {
        std::array<std::uint8_t, Size> result {};
        std::copy_n(octets.begin(), std::min(Size, octets.size()), result.begin());
        return result;
    }

    /// @brief Reads hexadecimal text into a fixed-size array.
    template <std::size_t Size>
    [[nodiscard]] std::array<std::uint8_t, Size> fixed(const std::string_view text) noexcept(false)
    {
        return fixed<Size>(cspan_uint8_t {hex(text)});
    }

    /// @brief Builds a key from octets.
    [[nodiscard]] inline kmx::aio::knx::secure::secret_key key(const cspan_uint8_t octets) noexcept
    {
        return kmx::aio::knx::secure::secret_key {fixed<kmx::aio::knx::secure::key_size>(octets)};
    }

    /// @brief Builds a key from hexadecimal text.
    [[nodiscard]] inline kmx::aio::knx::secure::secret_key key(const std::string_view text) noexcept(false)
    {
        return key(cspan_uint8_t {hex(text)});
    }

    /// @brief The EVP table with its MAC and CTR calls failing, to reach the crypto failure paths.
    [[nodiscard]] inline kmx::aio::knx::secure::detail::crypto_backend failing_backend() noexcept
    {
        auto backend = kmx::aio::knx::secure::detail::evp_backend();
        backend.cbc_mac = [](cspan_uint8_t, cspan_uint8_t, span_uint8_t) noexcept { return false; };
        backend.ctr = [](cspan_uint8_t, cspan_uint8_t, span_uint8_t, span_uint8_t) noexcept { return false; };
        return backend;
    }

    /// @brief Indicates whether every octet is zero.
    [[nodiscard]] inline bool all_zero(const cspan_uint8_t octets) noexcept
    {
        return std::ranges::all_of(octets, [](const std::uint8_t octet) noexcept { return octet == 0u; });
    }

    /// @brief One row of the generated vectors.
    struct vector_row
    {
        /// @brief The wire octets as text, for failure messages.
        std::string wire_text {};
        octets_t key {};
        octets_t timer_value {};
        octets_t serial_number {};
        octets_t message_tag {};
        /// @brief The plain datagram a wrapper carries; empty for a TIMER_NOTIFY.
        octets_t plain {};
        octets_t wire {};
    };

    /// @brief Finds the conformance directory from this header's own path.
    [[nodiscard]] inline std::filesystem::path conformance_directory() noexcept(false)
    {
        auto directory = std::filesystem::absolute(std::filesystem::path {__FILE__}).parent_path();
        while ((directory != directory.parent_path()) && !std::filesystem::exists(directory / "documentation"))
            directory = directory.parent_path();
        return directory / "documentation" / "features" / "knx" / "conformance";
    }

    /// @brief Splits one line at its tabs.
    [[nodiscard]] inline std::vector<std::string> split_tabs(const std::string& line) noexcept(false)
    {
        std::vector<std::string> fields {};
        std::size_t start {};
        for (auto tab = line.find('\t');; tab = line.find('\t', start))
        {
            fields.push_back(line.substr(start, tab - start));
            if (tab == std::string::npos)
                return fields;
            start = tab + 1u;
        }
    }

    /// @brief Reads the generated vectors of one kind: `wrapper` or `timer_notify`.
    [[nodiscard]] inline std::vector<vector_row> rows(const std::string_view kind) noexcept(false)
    {
        std::ifstream input(conformance_directory() / "secure-routing-vectors.tsv");
        std::vector<vector_row> result {};
        for (std::string line; std::getline(input, line);)
        {
            const auto fields = line.starts_with('#') ? std::vector<std::string> {} : split_tabs(line);
            if ((fields.size() != 7u) || (fields[0u] != kind))
                continue;
            result.push_back(vector_row {fields[6u], hex(fields[1u]), hex(fields[2u]), hex(fields[3u]), hex(fields[4u]), hex(fields[5u]),
                                         hex(fields[6u])});
        }

        return result;
    }
}
