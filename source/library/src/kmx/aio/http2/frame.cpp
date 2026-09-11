/// @file src/kmx/aio/http2/frame.cpp
/// @brief HTTP/2 GOAWAY frame serialization.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/http2/frame.hpp>
#ifndef PCH
    #include <kmx/aio/invalid_argument.hpp>

    #include <cstring>
    #include <stdexcept>
#endif

namespace kmx::aio::http2
{
    std::size_t make_goaway(span_uint8_t buffer, const std::uint32_t last_stream_id, const std::uint32_t error_code) noexcept(false)
    {
        if (buffer.size() < 17u)
            throw invalid_argument("Buffer too small for GOAWAY frame");

        std::memset(buffer.data(), 0, 9u);
        buffer[2u] = 0x08u; // 8 bytes long
        buffer[3u] = static_cast<std::uint8_t>(frame_type::goaway);

        // Payload: Last-Stream-ID (4 bytes), Error Code (4 bytes)
        buffer[9u] = (last_stream_id >> 24u) & 0xFFu;
        buffer[10u] = (last_stream_id >> 16u) & 0xFFu;
        buffer[11u] = (last_stream_id >> 8u) & 0xFFu;
        buffer[12u] = last_stream_id & 0xFFu;

        buffer[13u] = (error_code >> 24u) & 0xFFu;
        buffer[14u] = (error_code >> 16u) & 0xFFu;
        buffer[15u] = (error_code >> 8u) & 0xFFu;
        buffer[16u] = error_code & 0xFFu;

        return 17u;
    }

}
