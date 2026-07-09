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
    /// @brief AVTP AAF subtype used for AM824 audio transport.
    inline constexpr std::uint8_t subtype_aaf = 0x02u;
    /// @brief AM824 format identifier used in AAF payloads.
    inline constexpr std::uint8_t aaf_format_am824 = 0x10u;
    /// @brief Size in bytes of the fixed AM824/AAF header.
    inline constexpr std::size_t header_size = 24u;

    /// @brief Parsed view of an AM824 frame.
    struct am824_frame_view
    {
        /// @brief Stream identifier carried by the frame.
        stream_id_t stream_id {};
        /// @brief AVTP sequence number.
        std::uint8_t sequence_num {};
        /// @brief 32-bit AVTP presentation timestamp.
        std::uint32_t avtp_timestamp_32 {};
        /// @brief Payload bytes following the AM824 header.
        std::span<const std::byte> payload {};
    };

    /// @brief Converts a TAI timestamp in nanoseconds to the AVTP 32-bit timestamp field.
    /// @param tai_ns TAI time in nanoseconds.
    /// @return The encoded 32-bit AVTP timestamp.
    [[nodiscard]] std::uint32_t to_avtp_timestamp_32(avb_timestamp_t tai_ns) noexcept;

    /// @brief Expands a 32-bit AVTP timestamp into a full TAI timestamp near a reference.
    /// @param ts32 The 32-bit AVTP timestamp.
    /// @param reference_ns Reference TAI time in nanoseconds used to disambiguate wraparound.
    /// @return The expanded TAI timestamp.
    [[nodiscard]] avb_timestamp_t expand_avtp_timestamp_32(std::uint32_t ts32, avb_timestamp_t reference_ns) noexcept;

    /// @brief Builds an AM824 frame from a payload and presentation timestamp.
    /// @param stream_id Stream identifier to embed in the frame.
    /// @param sequence_num AVTP sequence number.
    /// @param presentation_time_ns Presentation time in TAI nanoseconds.
    /// @param payload Raw payload bytes.
    /// @return Encoded frame bytes or an error.
    [[nodiscard]] std::expected<std::vector<std::byte>, std::error_code> build_am824_frame(const stream_id_t& stream_id,
                                                                                           std::uint8_t sequence_num,
                                                                                           avb_timestamp_t presentation_time_ns,
                                                                                           std::span<const std::byte> payload) noexcept;

    /// @brief Parses an AM824 frame into a lightweight view.
    /// @param frame Encoded AM824 frame bytes.
    /// @return Parsed frame view or an error.
    [[nodiscard]] std::expected<am824_frame_view, std::error_code> parse_am824_frame(std::span<const std::byte> frame) noexcept;
} // namespace kmx::aio::avb::avtp
