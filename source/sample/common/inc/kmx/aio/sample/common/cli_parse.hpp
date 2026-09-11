/// @file inc/kmx/aio/sample/common/cli_parse.hpp
/// @brief Unsigned integer command-line argument parsers shared by the samples.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <string_view>
#endif

namespace kmx::aio::sample::common
{
    [[nodiscard]] bool parse_unsigned_u16(const std::string_view text, std::uint16_t& out) noexcept;
    [[nodiscard]] bool parse_unsigned_u64(const std::string_view text, std::uint64_t& out) noexcept;

    [[nodiscard]] bool parse_unsigned_u16_cstr(const char* raw, std::uint16_t& out) noexcept;
    [[nodiscard]] bool parse_unsigned_u32_cstr(const char* raw, std::uint32_t& out) noexcept;
    [[nodiscard]] bool parse_unsigned_u64_cstr(const char* raw, std::uint64_t& out) noexcept;
}
