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
    /// @brief Returns the encoded length of a QUIC variable-length integer.
    /// @param value The value to measure.
    /// @return The number of bytes the encoding occupies (1, 2, 4, or 8).
    [[nodiscard]] std::size_t varint_size(const std::uint64_t value) noexcept;

    /// @brief Appends a QUIC variable-length integer to a byte buffer.
    /// @param out   The buffer the encoding is appended to.
    /// @param value The value to encode.
    /// @throws std::bad_alloc If @p out could not grow.
    void encode_varint(std::vector<std::uint8_t>& out, const std::uint64_t value) noexcept(false);

    /// @brief Decodes a QUIC variable-length integer from a byte span.
    /// @param payload The bytes to decode from.
    /// @param offset  Index in @p payload the integer starts at.
    /// @return The decoded value paired with its encoded length, or a parse error.
    [[nodiscard]] std::expected<std::pair<std::uint64_t, std::size_t>, std::error_code> decode_varint(std::span<const std::uint8_t> payload,
                                                                                                      const std::size_t offset) noexcept;
} // namespace kmx::aio::http3::detail
