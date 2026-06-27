/// @file avb/avtp/am824.cpp
/// @brief AM824/AVTP framing helpers for AVB talker/listener samples.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <kmx/aio/avb/avtp/am824.hpp>

#include <cerrno>

namespace kmx::aio::avb::avtp
{
    [[nodiscard]] std::uint32_t to_avtp_timestamp_32(const avb_timestamp_t tai_ns) noexcept
    {
        return static_cast<std::uint32_t>(tai_ns & 0xFFFF'FFFFu);
    }

    [[nodiscard]] avb_timestamp_t expand_avtp_timestamp_32(const std::uint32_t ts32, const avb_timestamp_t reference_ns) noexcept
    {
        const auto hi = reference_ns & 0xFFFF'FFFF'0000'0000ULL;
        avb_timestamp_t candidate = hi | static_cast<avb_timestamp_t>(ts32);
        if (candidate + 0x8000'0000ULL < reference_ns)
            candidate += 0x1'0000'0000ULL;
        else if (candidate > reference_ns + 0x8000'0000ULL)
            candidate -= 0x1'0000'0000ULL;
        return candidate;
    }

    [[nodiscard]] std::expected<std::vector<std::byte>, std::error_code> build_am824_frame(const stream_id_t& stream_id,
                                                                                           const std::uint8_t sequence_num,
                                                                                           const avb_timestamp_t presentation_time_ns,
                                                                                           std::span<const std::byte> payload) noexcept
    {
        if (payload.size() > 0xFFFFu)
            return std::unexpected(error_from_errno(EINVAL));

        std::vector<std::byte> out(header_size + payload.size(), std::byte {0u});

        out[0] = static_cast<std::byte>(subtype_aaf);
        out[1] = static_cast<std::byte>(0x81u); // SV=1, version=0, TV=1
        out[2] = static_cast<std::byte>(sequence_num);
        out[3] = std::byte {0u};

        for (std::size_t i = 0; i < 6u; ++i)
            out[4u + i] = static_cast<std::byte>(stream_id.source_mac[i]);

        out[10] = static_cast<std::byte>(stream_id.unique_id >> 8u);
        out[11] = static_cast<std::byte>(stream_id.unique_id & 0xFFu);

        const std::uint32_t ts32 = to_avtp_timestamp_32(presentation_time_ns);
        out[12] = static_cast<std::byte>((ts32 >> 24u) & 0xFFu);
        out[13] = static_cast<std::byte>((ts32 >> 16u) & 0xFFu);
        out[14] = static_cast<std::byte>((ts32 >> 8u) & 0xFFu);
        out[15] = static_cast<std::byte>(ts32 & 0xFFu);

        const std::uint16_t len = static_cast<std::uint16_t>(payload.size());
        out[16] = static_cast<std::byte>(len >> 8u);
        out[17] = static_cast<std::byte>(len & 0xFFu);

        out[18] = static_cast<std::byte>(aaf_format_am824);
        out[19] = static_cast<std::byte>(0x05u); // 48 kHz nominal for sample app

        for (std::size_t i = 0; i < payload.size(); ++i)
            out[header_size + i] = payload[i];

        return out;
    }

    [[nodiscard]] std::expected<am824_frame_view, std::error_code> parse_am824_frame(std::span<const std::byte> frame) noexcept
    {
        if (frame.size() < header_size)
            return std::unexpected(error_from_errno(EINVAL));

        if (static_cast<std::uint8_t>(frame[0u]) != subtype_aaf)
            return std::unexpected(error_from_errno(EPROTO));

        const std::uint16_t payload_len = (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(frame[16u])) << 8u) |
                                          static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(frame[17u]));

        if (header_size + payload_len > frame.size())
            return std::unexpected(error_from_errno(EMSGSIZE));

        am824_frame_view out {};
        for (std::size_t i = 0; i < 6u; ++i)
            out.stream_id.source_mac[i] = std::to_integer<std::uint8_t>(frame[4u + i]);

        out.stream_id.unique_id = (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(frame[10u])) << 8u) |
                                  static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(frame[11u]));

        out.sequence_num = std::to_integer<std::uint8_t>(frame[2u]);

        out.avtp_timestamp_32 = (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(frame[12u])) << 24u) |
                                (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(frame[13u])) << 16u) |
                                (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(frame[14u])) << 8u) |
                                static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(frame[15u]));

        out.payload = frame.subspan(header_size, payload_len);
        return out;
    }
} // namespace kmx::aio::avb::avtp