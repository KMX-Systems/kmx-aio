/// @file api/kmx/aio/http3/qpack/literal_codec.hpp
/// @brief HTTP/3 QPACK literal header codec.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_HTTP3)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/http3/message.hpp>

        #include <cstdint>
        #include <expected>
        #include <optional>
        #include <string_view>
        #include <system_error>
        #include <vector>
    #endif

namespace kmx::aio::http3::qpack
{
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
        [[nodiscard]] static std::expected<header_list, std::error_code> decode(cspan_uint8_t payload) noexcept;
    };
}
#endif // KMX_AIO_FEATURE_HTTP3
