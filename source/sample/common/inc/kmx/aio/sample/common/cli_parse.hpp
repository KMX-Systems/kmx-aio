#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace kmx::aio::sample::common
{
    bool parse_unsigned_u16(std::string_view text, std::uint16_t& out);
    bool parse_unsigned_u64(std::string_view text, std::uint64_t& out);

    bool parse_unsigned_u16_cstr(const char* raw, std::uint16_t& out);
    bool parse_unsigned_u32_cstr(const char* raw, std::uint32_t& out);
    bool parse_unsigned_u64_cstr(const char* raw, std::uint64_t& out);

    bool parse_mac_bytes(std::string_view text, std::array<std::uint8_t, 6u>& out);
}
