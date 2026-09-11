/// @file api/kmx/aio/http3/data_codec.hpp
/// @brief HTTP/3 DATA frame codec.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_HTTP3)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>

        #include <cstdint>
        #include <expected>
        #include <system_error>
        #include <vector>
    #endif

namespace kmx::aio::http3
{
    /// @brief Explicit DATA frame codec.
    class data_codec
    {
    public:
        /// @brief Copies a DATA payload into owned storage.
        /// @param payload The payload bytes to encode.
        /// @return Serialized DATA payload bytes.
        static std::vector<std::uint8_t> encode(cspan_uint8_t payload) noexcept(false);
        /// @brief Copies a DATA payload out of owned storage.
        /// @param payload The encoded payload bytes.
        /// @return The decoded payload bytes or a parse error.
        [[nodiscard]] static std::expected<std::vector<std::uint8_t>, std::error_code> decode(cspan_uint8_t payload) noexcept;
        /// @brief Encodes a DATA frame containing the given payload.
        /// @param payload The payload bytes to encode.
        /// @return Serialized DATA frame bytes.
        static std::vector<std::uint8_t> encode_frame(cspan_uint8_t payload) noexcept(false);
        /// @brief Decodes a DATA frame and returns its payload.
        /// @param payload The encoded frame bytes.
        /// @return The decoded payload bytes or a parse error.
        [[nodiscard]] static std::expected<std::vector<std::uint8_t>, std::error_code> decode_frame(cspan_uint8_t payload) noexcept;
    };
}
#endif // KMX_AIO_FEATURE_HTTP3
