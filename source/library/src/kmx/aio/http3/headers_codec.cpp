/// @file src/kmx/aio/http3/headers_codec.cpp
/// @brief HTTP/3 HEADERS frame codec over the QPACK literal header codec.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/http3/headers_codec.hpp>
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/http3/frame.hpp>
    #include <kmx/aio/http3/frame_codec.hpp>
    #include <kmx/aio/http3/qpack/literal_codec.hpp>

    #include <cerrno>
#endif

namespace kmx::aio::http3
{
    std::vector<std::uint8_t> headers_codec::encode(const header_list& headers) noexcept(false)
    {
        return qpack::literal_codec::encode(headers);
    }

    std::expected<header_list, std::error_code> headers_codec::decode(cspan_uint8_t payload) noexcept
    {
        return qpack::literal_codec::decode(payload);
    }

    std::vector<std::uint8_t> headers_codec::encode_frame(const header_list& headers) noexcept(false)
    {
        const auto block = encode(headers);
        return frame_codec::encode(frame_type::headers, block);
    }

    std::expected<header_list, std::error_code> headers_codec::decode_frame(cspan_uint8_t payload) noexcept
    {
        auto decoded = frame_codec::decode(payload);
        if (!decoded)
            return std::unexpected(decoded.error());
        if (decoded->type != frame_type::headers)
            return std::unexpected(kmx::aio::error_from_errno(EINVAL));
        return decode(decoded->payload);
    }
}
