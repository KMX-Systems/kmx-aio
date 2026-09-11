/// @file api/kmx/aio/http3/headers_codec.hpp
/// @brief HTTP/3 HEADERS frame codec.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_HTTP3)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/http3/message.hpp>

        #include <cstdint>
        #include <expected>
        #include <system_error>
        #include <vector>
    #endif

namespace kmx::aio::http3
{
    /// @brief Explicit HEADERS frame codec built on top of the QPACK layer.
    class headers_codec
    {
    public:
        /// @brief Encodes header fields into a literal header block.
        /// @param headers The header fields to encode.
        /// @return Encoded header block bytes.
        static std::vector<std::uint8_t> encode(const header_list& headers) noexcept(false);
        /// @brief Decodes a literal header block into header fields.
        /// @param payload The encoded header block bytes.
        /// @return The decoded header list or a parse error.
        [[nodiscard]] static std::expected<header_list, std::error_code> decode(cspan_uint8_t payload) noexcept;
        /// @brief Encodes a HEADERS frame containing the given headers.
        /// @param headers The header fields to encode.
        /// @return Serialized HEADERS frame bytes.
        static std::vector<std::uint8_t> encode_frame(const header_list& headers) noexcept(false);
        /// @brief Decodes a HEADERS frame and returns its header fields.
        /// @param payload The encoded frame bytes.
        /// @return The decoded header list or a parse error.
        [[nodiscard]] static std::expected<header_list, std::error_code> decode_frame(cspan_uint8_t payload) noexcept;
    };
}
#endif // KMX_AIO_FEATURE_HTTP3
