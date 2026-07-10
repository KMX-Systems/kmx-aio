/// @file aio/http3/qpack.hpp
/// @brief HTTP/3 QPACK definitions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <kmx/aio/http3/message.hpp>

#include <expected>
#include <optional>
#include <span>
#include <system_error>
#include <vector>

namespace kmx::aio::http3::qpack
{
    /// @brief Encoded field representation tags used by the demo literal codec.
    enum class field_representation : std::uint8_t
    {
        /// @brief Literal field with inline name.
        literal_with_name = 0x00u,
        /// @brief Literal field with indexed name.
        literal_with_name_ref = 0x40u,
        /// @brief Indexed field from the static table.
        indexed_field = 0x80u,
    };

    /// @brief Minimal literal-only QPACK-like codec for demo/prototyping use.
    /// @details Encodes a stable header block with zero required insert count /
    /// zero delta base semantics
    ///          and raw literal header fields. This is intentionally not a full
    ///          RFC-complete QPACK implementation, but it gives the HTTP/3
    ///          layer a dedicated header-compression namespace and replaceable
    ///          abstraction.
    class literal_codec
    {
    public:
        /// @brief Finds a static table entry matching a header name.
        /// @param name The header name.
        /// @return The static index if present.
        static std::optional<std::uint64_t> static_name_index(const std::string_view name) noexcept;
        /// @brief Finds a static table entry matching a header name and value.
        /// @param name The header name.
        /// @param value The header value.
        /// @return The static index if present.
        static std::optional<std::uint64_t> static_field_index(const std::string_view name, const std::string_view value) noexcept;
        /// @brief Encodes headers into the demo literal header block format.
        /// @param headers The header list to encode.
        /// @return Encoded header block bytes.
        static std::vector<std::uint8_t> encode(const header_list& headers) noexcept(false);
        /// @brief Decodes a demo literal header block.
        /// @param payload Encoded header block bytes.
        /// @return The decoded header list or a parsing error.
        static std::expected<header_list, std::error_code> decode(std::span<const std::uint8_t> payload) noexcept;
    };
} // namespace kmx::aio::http3::qpack