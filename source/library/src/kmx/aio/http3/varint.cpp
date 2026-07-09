#include "varint.hpp"

namespace kmx::aio::http3::detail
{
    [[nodiscard]] std::size_t varint_size(const std::uint64_t value) noexcept
    {
        if (value <= 63u)
            return 1u;
        if (value <= 16383u)
            return 2u;
        if (value <= 1073741823u)
            return 4u;
        return 8u;
    }

    void encode_varint(std::vector<std::uint8_t>& out, const std::uint64_t value) noexcept(false)
    {
        if (value <= 63u)
        {
            out.push_back(static_cast<std::uint8_t>(value));
            return;
        }

        if (value <= 16383u)
        {
            out.push_back(static_cast<std::uint8_t>(0x40u | ((value >> 8u) & 0x3Fu)));
            out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
            return;
        }

        if (value <= 1073741823u)
        {
            out.push_back(static_cast<std::uint8_t>(0x80u | ((value >> 24u) & 0x3Fu)));
            out.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
            out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
            return;
        }

        out.push_back(static_cast<std::uint8_t>(0xC0u | ((value >> 56u) & 0x3Fu)));
        out.push_back(static_cast<std::uint8_t>((value >> 48u) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((value >> 40u) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((value >> 32u) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    }

    [[nodiscard]] std::expected<std::pair<std::uint64_t, std::size_t>, std::error_code> decode_varint(std::span<const std::uint8_t> payload,
                                                                                                      const std::size_t offset) noexcept
    {
        if (offset >= payload.size())
            return std::unexpected(kmx::aio::error_from_errno(EINVAL));

        const std::uint8_t first = payload[offset];
        const std::uint8_t prefix = first >> 6u;
        const std::size_t length = std::size_t {1u} << prefix;
        if (offset + length > payload.size())
            return std::unexpected(kmx::aio::error_from_errno(EINVAL));

        std::uint64_t value = static_cast<std::uint64_t>(first & 0x3Fu);
        for (std::size_t index = 1u; index < length; ++index)
            value = (value << 8u) | payload[offset + index];

        return std::pair<std::uint64_t, std::size_t> {value, length};
    }
} // namespace kmx::aio::http3::detail
