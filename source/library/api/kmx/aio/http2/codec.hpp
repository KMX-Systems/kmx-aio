#pragma once

#include "frame.hpp"
#include "hpack.hpp"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace kmx::aio::http2
{

    /// @brief Utility class to dynamically build HTTP/2 frames.
    class frame_builder
    {
    public:
        /// @brief Format an HTTP/2 SETTINGS frame into a buffer
        /// @param buffer Destination buffer
        /// @return Number of bytes written
        static std::size_t make_settings(std::span<std::uint8_t> buffer) noexcept(false);

        /// @brief Format an HTTP/2 SETTINGS ACK frame into a buffer
        /// @param buffer Destination buffer
        /// @return Number of bytes written
        static std::size_t make_settings_ack(std::span<std::uint8_t> buffer) noexcept(false);

        /// @brief Format an HTTP/2 HEADERS frame dynamically using HPACK
        /// @param buffer Destination buffer
        /// @param stream_id The internal stream identifier
        /// @param end_stream True if this frame also signals END_STREAM
        /// @param headers List of key-value string pairs to encode
        /// @return Number of bytes written
        static std::size_t make_headers(std::span<std::uint8_t> buffer, std::uint32_t stream_id, bool end_stream,
                                        const header_list& headers) noexcept(false);

        /// @brief Format an HTTP/2 DATA frame into a buffer
        /// @param buffer Destination buffer
        /// @param stream_id The internal stream identifier
        /// @param end_stream True if this is the final data frame
        /// @param data The raw data to append
        /// @return Number of bytes written
        static std::size_t make_data(std::span<std::uint8_t> buffer, std::uint32_t stream_id, bool end_stream,
                                     std::string_view data) noexcept(false);
    };

} // namespace kmx::aio::http2
