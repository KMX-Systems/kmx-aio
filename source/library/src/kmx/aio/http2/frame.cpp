#include <kmx/aio/http2/frame.hpp>

#include <cstring>
#include <stdexcept>

namespace kmx::aio::http2
{

    std::size_t make_goaway(std::span<std::uint8_t> buffer, std::uint32_t last_stream_id, std::uint32_t error_code) noexcept(false)
    {
        if (buffer.size() < 17u)
        {
            throw std::invalid_argument("Buffer too small for GOAWAY frame");
        }

        std::memset(buffer.data(), 0, 9u);
        buffer[2] = 0x08u; // 8 bytes long
        buffer[3] = static_cast<std::uint8_t>(frame_type::goaway);

        // Payload: Last-Stream-ID (4 bytes), Error Code (4 bytes)
        buffer[9] = (last_stream_id >> 24u) & 0xFFu;
        buffer[10] = (last_stream_id >> 16u) & 0xFFu;
        buffer[11] = (last_stream_id >> 8u) & 0xFFu;
        buffer[12] = last_stream_id & 0xFFu;

        buffer[13] = (error_code >> 24u) & 0xFFu;
        buffer[14] = (error_code >> 16u) & 0xFFu;
        buffer[15] = (error_code >> 8u) & 0xFFu;
        buffer[16] = error_code & 0xFFu;

        return 17u;
    }

} // namespace kmx::aio::http2
