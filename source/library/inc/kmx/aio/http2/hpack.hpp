#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

/// @brief HTTP/2 core protocol definitions and utilities
namespace kmx::aio::http2
{

    using header_field = std::pair<std::string_view, std::string_view>;
    using header_list = std::vector<header_field>;

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
        static constexpr std::size_t encoded_size_literal(std::string_view name, std::string_view value) noexcept
        {
            return 1 + 1 + name.size() + 1 + value.size();
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
        static std::size_t encode_literal(std::span<std::uint8_t> buffer, std::string_view name, std::string_view value) noexcept(false);

        /// @brief Encodes multiple headers into a continuous HPACK block within a span
        /// @param buffer Destination buffer
        /// @param headers List of key/value pairs representing headers
        /// @return Number of bytes written
        /// @throws std::invalid_argument if buffer is too small
        static std::size_t encode(std::span<std::uint8_t> buffer, const header_list& headers) noexcept(false);
    };

} // namespace kmx::aio::http2
