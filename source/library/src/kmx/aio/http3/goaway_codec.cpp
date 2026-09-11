/// @file src/kmx/aio/http3/goaway_codec.cpp
/// @brief HTTP/3 GOAWAY payload and frame encoding and decoding.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/http3/goaway_codec.hpp>
#ifndef PCH
    #include <kmx/aio/http3/detail/varint.hpp>
    #include <kmx/aio/http3/frame.hpp>
    #include <kmx/aio/http3/frame_codec.hpp>
#endif

namespace kmx::aio::http3
{
    std::vector<std::uint8_t> goaway_codec::encode(const goaway_frame& value) noexcept(false)
    {
        std::vector<std::uint8_t> payload;
        payload.reserve(detail::varint_size(value.stream_id));
        detail::encode_varint(payload, value.stream_id);
        return payload;
    }

    std::expected<goaway_frame, std::error_code> goaway_codec::decode(cspan_uint8_t payload) noexcept
    {
        auto decoded = detail::decode_varint(payload, 0u);
        if (!decoded)
            return std::unexpected(make_error_code(error_code::id_error));
        if (decoded->second != payload.size())
            return std::unexpected(make_error_code(error_code::id_error));
        return goaway_frame {.stream_id = decoded->first};
    }

    std::vector<std::uint8_t> goaway_codec::encode_frame(const goaway_frame& value) noexcept(false)
    {
        const auto payload = encode(value);
        return frame_codec::encode(frame_type::goaway, payload);
    }

    std::expected<goaway_frame, std::error_code> goaway_codec::decode_frame(cspan_uint8_t payload) noexcept
    {
        auto decoded = frame_codec::decode(payload);
        if (!decoded)
            return std::unexpected(decoded.error());
        if (decoded->type != frame_type::goaway)
            return std::unexpected(make_error_code(error_code::frame_unexpected));
        return decode(decoded->payload);
    }
}
