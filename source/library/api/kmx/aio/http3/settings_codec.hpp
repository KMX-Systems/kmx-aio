/// @file api/kmx/aio/http3/settings_codec.hpp
/// @brief HTTP/3 SETTINGS payload and frame codec.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_HTTP3)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/http3/settings.hpp>

        #include <cstdint>
        #include <expected>
        #include <system_error>
        #include <vector>
    #endif

namespace kmx::aio::http3
{
    /// @brief Helpers for HTTP/3 SETTINGS payload encoding/decoding.
    class settings_codec
    {
    public:
        /// @brief Encodes HTTP/3 settings into a payload block.
        /// @param value The settings model to encode.
        /// @return Serialized settings payload bytes.
        static std::vector<std::uint8_t> encode(const settings& value) noexcept(false);
        /// @brief Decodes a settings payload block.
        /// @param payload The encoded payload bytes.
        /// @return The decoded settings or a parse error.
        [[nodiscard]] static std::expected<settings, std::error_code> decode(cspan_uint8_t payload) noexcept;
        /// @brief Encodes a SETTINGS frame containing the given settings.
        /// @param value The settings model to encode.
        /// @return Serialized SETTINGS frame bytes.
        static std::vector<std::uint8_t> encode_frame(const settings& value) noexcept(false);
        /// @brief Decodes a SETTINGS frame and returns the settings payload.
        /// @param payload The encoded frame bytes.
        /// @return The decoded settings or a parse error.
        [[nodiscard]] static std::expected<settings, std::error_code> decode_frame(cspan_uint8_t payload) noexcept;
    };
}
#endif // KMX_AIO_FEATURE_HTTP3
