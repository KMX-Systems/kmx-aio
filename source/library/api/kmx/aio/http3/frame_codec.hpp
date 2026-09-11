/// @file api/kmx/aio/http3/frame_codec.hpp
/// @brief HTTP/3 frame envelope codec.
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
    /// @brief HTTP/3 frame envelope encode/decode helpers.
    class frame_codec
    {
    public:
        /// @brief Encodes a frame envelope from a type and payload.
        /// @param type The HTTP/3 frame type.
        /// @param payload The frame payload bytes.
        /// @return Serialized frame envelope bytes.
        static std::vector<std::uint8_t> encode(frame_type type, cspan_uint8_t payload) noexcept(false);
        /// @brief Decodes a single frame envelope.
        /// @param payload The encoded frame envelope bytes.
        /// @return The decoded frame or a parse error.
        [[nodiscard]] static std::expected<frame, std::error_code> decode(cspan_uint8_t payload) noexcept;
        /// @brief Decodes all frame envelopes contained in a buffer.
        /// @param payload The encoded byte buffer.
        /// @return All decoded frames or a parse error.
        [[nodiscard]] static std::expected<std::vector<frame>, std::error_code> decode_all(cspan_uint8_t payload) noexcept;
    };
}
#endif // KMX_AIO_FEATURE_HTTP3
