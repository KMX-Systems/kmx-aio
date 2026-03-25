#include <kmx/aio/http2/codec.hpp>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace kmx::aio::http2
{

    std::size_t frame_builder::make_settings(std::span<std::uint8_t> buffer) noexcept(false)
    {
        if (buffer.size() < 9u)
        {
            throw std::invalid_argument("Buffer too small for SETTINGS frame");
        }
        std::memset(buffer.data(), 0, 9u);
        buffer[3] = static_cast<std::uint8_t>(frame_type::settings);
        return 9u;
    }

    std::size_t frame_builder::make_settings_ack(std::span<std::uint8_t> buffer) noexcept(false)
    {
        std::size_t written = make_settings(buffer);
        buffer[4] = 0x01u; // Flags: ACK
        return written;
    }

    std::size_t frame_builder::make_headers(std::span<std::uint8_t> buffer, std::uint32_t stream_id, bool end_stream,
                                            const header_list& headers) noexcept(false)
    {
        std::size_t hpack_len = hpack_encoder::encoded_size(headers);
        if (hpack_len > 0xFFFFFFu)
        {
            throw std::runtime_error("Header block too large");
        }
        if (buffer.size() < 9u + hpack_len)
        {
            throw std::invalid_argument("Buffer too small for HEADERS frame");
        }

        // Write Frame Header
        std::uint32_t len = static_cast<std::uint32_t>(hpack_len);
        buffer[0] = (len >> 16u) & 0xFFu;
        buffer[1] = (len >> 8u) & 0xFFu;
        buffer[2] = len & 0xFFu;

        buffer[3] = static_cast<std::uint8_t>(frame_type::headers);
        buffer[4] = 0x04u; // Flags: END_HEADERS (0x04)
        if (end_stream)
        {
            buffer[4] |= 0x01u; // OR in END_STREAM (0x01)
        }

        buffer[5] = (stream_id >> 24u) & 0xFFu;
        buffer[6] = (stream_id >> 16u) & 0xFFu;
        buffer[7] = (stream_id >> 8u) & 0xFFu;
        buffer[8] = stream_id & 0xFFu;

        // Inject HPACK payload
        hpack_encoder::encode(buffer.subspan(9u, hpack_len), headers);

        return 9u + hpack_len;
    }

    std::size_t frame_builder::make_data(std::span<std::uint8_t> buffer, std::uint32_t stream_id, bool end_stream,
                                         std::string_view data) noexcept(false)
    {
        if (data.size() > 0xFFFFFFu)
        {
            throw std::runtime_error("Data block too large");
        }
        if (buffer.size() < 9u + data.size())
        {
            throw std::invalid_argument("Buffer too small for DATA frame");
        }

        std::uint32_t len = static_cast<std::uint32_t>(data.size());
        buffer[0] = (len >> 16u) & 0xFFu;
        buffer[1] = (len >> 8u) & 0xFFu;
        buffer[2] = len & 0xFFu;

        buffer[3] = static_cast<std::uint8_t>(frame_type::data);
        buffer[4] = end_stream ? 0x01u : 0x00u; // Flags: END_STREAM (0x01)

        buffer[5] = (stream_id >> 24u) & 0xFFu;
        buffer[6] = (stream_id >> 16u) & 0xFFu;
        buffer[7] = (stream_id >> 8u) & 0xFFu;
        buffer[8] = stream_id & 0xFFu;

        // Copy raw payload
        for (std::size_t i{}; i < data.size(); ++i)
        {
            buffer[9u + i] = static_cast<std::uint8_t>(data[i]);
        }

        return 9u + data.size();
    }

} // namespace kmx::aio::http2
