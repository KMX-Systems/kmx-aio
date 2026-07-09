#pragma once

#include <kmx/aio/basic_types.hpp>

#include <cstddef>
#include <cstdint>
#ifndef PCH
    #include <expected>
    #include <span>
    #include <system_error>
    #include <utility>
    #include <vector>
#endif

namespace kmx::aio::http3::detail
{
    [[nodiscard]] std::size_t varint_size(const std::uint64_t value) noexcept;

    void encode_varint(std::vector<std::uint8_t>& out, const std::uint64_t value) noexcept(false);

    [[nodiscard]] std::expected<std::pair<std::uint64_t, std::size_t>, std::error_code> decode_varint(std::span<const std::uint8_t> payload,
                                                                                                      const std::size_t offset) noexcept;
} // namespace kmx::aio::http3::detail
