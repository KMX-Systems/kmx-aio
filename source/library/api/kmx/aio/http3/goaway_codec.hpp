/// @file api/kmx/aio/http3/goaway_codec.hpp
/// @brief HTTP/3 GOAWAY payload and frame codec.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_HTTP3)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/http3/frame.hpp>

        #include <cstdint>
        #include <expected>
        #include <system_error>
        #include <vector>
    #endif

namespace kmx::aio::http3
{
    /// @brief Encodes and decodes HTTP/3 GOAWAY payloads and frames.
    class goaway_codec
    {
    public:
        /// @brief Encodes a GOAWAY payload.
        /// @param value The GOAWAY frame payload to encode.
        /// @return Serialized GOAWAY payload bytes.
        static std::vector<std::uint8_t> encode(const goaway_frame& value) noexcept(false);
        /// @brief Decodes a GOAWAY payload.
        /// @param payload The encoded payload bytes.
        /// @return The decoded GOAWAY payload or a parse error.
        [[nodiscard]] static std::expected<goaway_frame, std::error_code> decode(cspan_uint8_t payload) noexcept;
        /// @brief Encodes a GOAWAY frame containing the given payload.
        /// @param value The GOAWAY frame payload to encode.
        /// @return Serialized GOAWAY frame bytes.
        static std::vector<std::uint8_t> encode_frame(const goaway_frame& value) noexcept(false);
        /// @brief Decodes a GOAWAY frame and returns its payload.
        /// @param payload The encoded frame bytes.
        /// @return The decoded GOAWAY payload or a parse error.
        [[nodiscard]] static std::expected<goaway_frame, std::error_code> decode_frame(cspan_uint8_t payload) noexcept;
    };
}
#endif // KMX_AIO_FEATURE_HTTP3
