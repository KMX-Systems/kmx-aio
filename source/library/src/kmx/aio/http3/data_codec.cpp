/// @file src/kmx/aio/http3/data_codec.cpp
/// @brief HTTP/3 DATA payload and frame codec.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/http3/data_codec.hpp>
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/http3/frame.hpp>
    #include <kmx/aio/http3/frame_codec.hpp>

    #include <cerrno>
    #include <utility>
#endif

namespace kmx::aio::http3
{
    std::vector<std::uint8_t> data_codec::encode(cspan_uint8_t payload) noexcept(false)
    {
        return std::vector<std::uint8_t>(payload.begin(), payload.end());
    }

    std::expected<std::vector<std::uint8_t>, std::error_code> data_codec::decode(cspan_uint8_t payload) noexcept
    {
        return std::vector<std::uint8_t>(payload.begin(), payload.end());
    }

    std::vector<std::uint8_t> data_codec::encode_frame(cspan_uint8_t payload) noexcept(false)
    {
        // Straight to the framer. encode() is the identity on a DATA payload, so materializing its
        // result first only copies the whole body into a buffer whose sole use is to be copied again.
        return frame_codec::encode(frame_type::data, payload);
    }

    std::expected<std::vector<std::uint8_t>, std::error_code> data_codec::decode_frame(cspan_uint8_t payload) noexcept
    {
        auto decoded = frame_codec::decode(payload);
        if (!decoded)
            return std::unexpected(decoded.error());
        if (decoded->type != frame_type::data)
            return std::unexpected(kmx::aio::error_from_errno(EINVAL));

        // decode() would copy the payload the framer has already extracted, into a buffer this frame
        // is about to drop. Handing the frame's own storage over says the same thing without the copy.
        return std::move(decoded->payload);
    }
}
