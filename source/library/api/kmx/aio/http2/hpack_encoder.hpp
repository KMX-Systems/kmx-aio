/// @file api/kmx/aio/http2/hpack_encoder.hpp
/// @brief HTTP/2 HPACK encoder definitions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_HTTP2)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/http2/hpack.hpp>

        #include <cstddef>
        #include <cstdint>
        #include <span>
        #include <string_view>
    #endif

namespace kmx::aio::http2
{
    /// @brief A minimal, zero-dependency HPACK encoder.
    /// Currently implements "Literal Header Field without Indexing"
    /// to avoid dependency on large Huffman tables while satisfying dynamic generation.
    class hpack_encoder
    {
    public:
        /// @brief Computes the exact encoded size for a single literal header.
        /// @param name The HTTP/2 header name
        /// @param value The HTTP/2 header value
        /// @return The size in bytes
        static constexpr std::size_t encoded_size_literal(const std::string_view name, const std::string_view value) noexcept
        {
            return 1u + 1u + name.size() + 1u + value.size();
        }

        /// @brief Computes the exact encoded size for multiple literal headers.
        /// @param headers List of key/value pairs representing headers
        /// @return The size in bytes
        static std::size_t encoded_size(const header_list& headers) noexcept;

        /// @brief Encodes a single HTTP/2 header pair dynamically into a span
        /// @param buffer Destination buffer
        /// @param name The HTTP/2 header name (e.g., ":method")
        /// @param value The HTTP/2 header value (e.g., "GET")
        /// @return Number of bytes written
        /// @throws std::invalid_argument if buffer is too small
        static std::size_t encode_literal(span_uint8_t buffer, std::string_view name, std::string_view value) noexcept(false);

        /// @brief Encodes multiple headers into a continuous HPACK block within a span
        /// @param buffer Destination buffer
        /// @param headers List of key/value pairs representing headers
        /// @return Number of bytes written
        /// @throws std::invalid_argument if buffer is too small
        static std::size_t encode(span_uint8_t buffer, const header_list& headers) noexcept(false);
    };
}
#endif // KMX_AIO_FEATURE_HTTP2
