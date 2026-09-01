#include <kmx/aio/http2/hpack.hpp>

#include <cstring>
#include <stdexcept>

namespace kmx::aio::http2
{

    std::size_t hpack_encoder::encoded_size(const header_list& headers) noexcept
    {
        std::size_t total {};
        for (const auto& kv: headers)
            total += encoded_size_literal(kv.first, kv.second);

        return total;
    }

    std::size_t hpack_encoder::encode_literal(span_uint8_t buffer, const std::string_view name,
                                              const std::string_view value) noexcept(false)
    {
        std::size_t required = encoded_size_literal(name, value);
        if (buffer.size() < required)
            throw std::invalid_argument("Buffer too small for HPACK literal encoding");

        std::size_t offset {};

        // 0x00 = Literal Header Field without Indexing (New Name)
        buffer[offset++] = 0x00u;

        // Name Length (Assuming length < 127 for minimal implementation)
        buffer[offset++] = static_cast<std::uint8_t>(name.size() & 0x7Fu);
        if (!name.empty())
        {
            std::memcpy(buffer.data() + offset, name.data(), name.size());
            offset += name.size();
        }

        // Value Length (Assuming length < 127 for minimal implementation)
        buffer[offset++] = static_cast<std::uint8_t>(value.size() & 0x7Fu);
        if (!value.empty())
        {
            std::memcpy(buffer.data() + offset, value.data(), value.size());
            offset += value.size();
        }

        return offset;
    }

    std::size_t hpack_encoder::encode(span_uint8_t buffer, const header_list& headers) noexcept(false)
    {
        std::size_t offset {};
        for (const auto& kv: headers)
        {
            const std::size_t chunk_size = encode_literal(buffer.subspan(offset), kv.first, kv.second);
            offset += chunk_size;
        }

        return offset;
    }

} // namespace kmx::aio::http2
