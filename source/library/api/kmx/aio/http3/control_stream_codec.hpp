/// @file api/kmx/aio/http3/control_stream_codec.hpp
/// @brief HTTP/3 control stream codec.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_HTTP3)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/http3/control.hpp>
        #include <kmx/aio/http3/frame.hpp>
        #include <kmx/aio/http3/settings.hpp>

        #include <cstdint>
        #include <expected>
        #include <system_error>
        #include <vector>
    #endif

namespace kmx::aio::http3
{
    /// @brief Builds and extends the byte stream of an HTTP/3 control stream.
    class control_stream_codec
    {
    public:
        /// @brief Encodes the opening control stream bytes.
        /// @param value The initial settings to place on the control stream.
        /// @return Serialized control stream bytes.
        static std::vector<std::uint8_t> encode_opening(const settings& value) noexcept(false);
        /// @brief Appends a GOAWAY frame to an existing control stream byte buffer.
        /// @param control_stream_bytes The current control stream bytes.
        /// @param value The GOAWAY payload to append.
        /// @return The updated control stream bytes.
        static std::vector<std::uint8_t> append_goaway(cspan_uint8_t control_stream_bytes, const goaway_frame& value) noexcept(false);
        /// @brief Decodes a complete control stream byte buffer.
        /// @param payload The encoded control stream bytes.
        /// @return The decoded control stream state or a protocol error.
        [[nodiscard]] static std::expected<control_stream_state, std::error_code> decode(cspan_uint8_t payload) noexcept;
    };
}
#endif // KMX_AIO_FEATURE_HTTP3
