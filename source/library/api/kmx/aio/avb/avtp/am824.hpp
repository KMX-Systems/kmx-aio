/// @file avb/avtp/am824.hpp
/// @brief Minimal AVTP AAF/AM824 framing helpers for AVB talker/listener samples.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>
    #include <expected>
    #include <span>
    #include <system_error>
    #include <vector>

    #include <kmx/aio/avb/avb_types.hpp>
    #include <kmx/aio/basic_types.hpp>
#endif

namespace kmx::aio::avb::avtp
{
    inline constexpr std::uint8_t subtype_aaf = 0x02u;
    inline constexpr std::uint8_t aaf_format_am824 = 0x10u;
    inline constexpr std::size_t header_size = 24u;

    struct am824_frame_view
    {
        stream_id_t stream_id {};
        std::uint8_t sequence_num {};
        std::uint32_t avtp_timestamp_32 {};
        std::span<const std::byte> payload {};
    };

    [[nodiscard]] std::uint32_t to_avtp_timestamp_32(avb_timestamp_t tai_ns) noexcept;

    [[nodiscard]] avb_timestamp_t expand_avtp_timestamp_32(std::uint32_t ts32, avb_timestamp_t reference_ns) noexcept;

    [[nodiscard]] std::expected<std::vector<std::byte>, std::error_code> build_am824_frame(const stream_id_t& stream_id,
                                                                                           std::uint8_t sequence_num,
                                                                                           avb_timestamp_t presentation_time_ns,
                                                                                           std::span<const std::byte> payload) noexcept;

    [[nodiscard]] std::expected<am824_frame_view, std::error_code> parse_am824_frame(std::span<const std::byte> frame) noexcept;
} // namespace kmx::aio::avb::avtp
