/// @file src/kmx/aio/http3/frame_codec.cpp
/// @brief HTTP/3 frame envelope encoding and decoding.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/http3/frame_codec.hpp>
#ifndef PCH
    #include <kmx/aio/http3/detail/varint.hpp>
    #include <kmx/aio/http3/frame.hpp>

    #include <cstddef>
    #include <utility>
#endif

namespace kmx::aio::http3
{
    namespace detail
    {
        [[nodiscard]] std::error_code frame_parse_error() noexcept
        {
            return make_error_code(error_code::frame_error);
        }
    }

    std::vector<std::uint8_t> frame_codec::encode(const frame_type type, cspan_uint8_t payload) noexcept(false)
    {
        std::vector<std::uint8_t> encoded;
        const std::size_t exact_capacity =
            detail::varint_size(static_cast<std::uint64_t>(type)) + detail::varint_size(payload.size()) + payload.size();
        encoded.reserve(exact_capacity);
        detail::encode_varint(encoded, static_cast<std::uint64_t>(type));
        detail::encode_varint(encoded, payload.size());
        encoded.insert(encoded.end(), payload.begin(), payload.end());
        return encoded;
    }

    std::expected<frame, std::error_code> frame_codec::decode(cspan_uint8_t payload) noexcept
    {
        auto type_result = detail::decode_varint(payload, 0u);
        if (!type_result)
            return std::unexpected(detail::frame_parse_error());

        auto length_result = detail::decode_varint(payload, type_result->second);
        if (!length_result)
            return std::unexpected(detail::frame_parse_error());

        const std::size_t payload_offset = type_result->second + length_result->second;
        if (payload_offset + length_result->first > payload.size())
            return std::unexpected(detail::frame_parse_error());

        frame decoded {};
        decoded.type = static_cast<frame_type>(type_result->first);
        decoded.payload.assign(payload.begin() + static_cast<std::ptrdiff_t>(payload_offset),
                               payload.begin() + static_cast<std::ptrdiff_t>(payload_offset + length_result->first));
        return decoded;
    }

    std::expected<std::vector<frame>, std::error_code> frame_codec::decode_all(cspan_uint8_t payload) noexcept
    {
        std::vector<frame> frames;
        frames.reserve(2u);
        std::size_t offset {};
        while (offset < payload.size())
        {
            auto type_result = detail::decode_varint(payload, offset);
            if (!type_result)
                return std::unexpected(detail::frame_parse_error());

            auto length_result = detail::decode_varint(payload, offset + type_result->second);
            if (!length_result)
                return std::unexpected(detail::frame_parse_error());

            const std::size_t payload_offset = offset + type_result->second + length_result->second;
            if (payload_offset + length_result->first > payload.size())
                return std::unexpected(detail::frame_parse_error());

            frame decoded {};
            decoded.type = static_cast<frame_type>(type_result->first);
            decoded.payload.assign(payload.begin() + static_cast<std::ptrdiff_t>(payload_offset),
                                   payload.begin() + static_cast<std::ptrdiff_t>(payload_offset + length_result->first));
            frames.push_back(std::move(decoded));
            offset = payload_offset + static_cast<std::size_t>(length_result->first);
        }

        return frames;
    }
}
