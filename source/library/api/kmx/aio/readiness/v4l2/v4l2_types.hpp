/// @file aio/readiness/v4l2/v4l2_types.hpp
/// @brief V4L2 capture types shared across the V4L2 pillar.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <cstdint>
#include <string>

namespace kmx::aio::readiness::v4l2
{
    /// @brief Pixel format described by a V4L2 FourCC code.
    struct pixel_format
    {
        std::uint32_t fourcc {};

        [[nodiscard]] constexpr bool operator==(const pixel_format&) const noexcept = default;
    };

    /// @brief Common V4L2 pixel format constants.
    namespace fourcc
    {
        /// @brief Packed YUV 4:2:2, 16 bpp. Most widely supported by USB webcams.
        inline constexpr pixel_format yuyv { 0x56595559u }; // V4L2_PIX_FMT_YUYV

        /// @brief MJPEG compressed frames. Preferred for high-resolution USB cameras.
        inline constexpr pixel_format mjpeg { 0x47504A4Du }; // V4L2_PIX_FMT_MJPEG

        /// @brief NV12: semi-planar YUV 4:2:0. Common on ISP and CSI-2 pipelines (GMSL).
        inline constexpr pixel_format nv12 { 0x3231564Eu }; // V4L2_PIX_FMT_NV12

        /// @brief RGB24: packed 24-bit RGB. Useful for display pipelines.
        inline constexpr pixel_format rgb24 { 0x33424752u }; // V4L2_PIX_FMT_RGB24

        /// @brief H.264 encoded stream. Available on encoder-capable capture devices.
        inline constexpr pixel_format h264 { 0x34363248u }; // V4L2_PIX_FMT_H264
    } // namespace fourcc

    /// @brief Frame resolution in pixels.
    struct frame_size
    {
        std::uint32_t width {};
        std::uint32_t height {};

        [[nodiscard]] constexpr bool operator==(const frame_size&) const noexcept = default;
    };

    /// @brief Common frame rate expressed as a rational number (numerator / denominator).
    struct frame_rate
    {
        std::uint32_t numerator { 1u };
        std::uint32_t denominator { 30u }; ///< Default: 30 fps

        [[nodiscard]] constexpr bool operator==(const frame_rate&) const noexcept = default;
    };

    /// @brief Configuration for a V4L2 capture device.
    struct capture_config
    {
        std::string    device       { "/dev/video0" }; ///< Device node path.
        pixel_format   format       { fourcc::yuyv };  ///< Desired pixel format (driver may adjust).
        frame_size     size         { 640u, 480u };    ///< Desired frame resolution.
        frame_rate     fps          {};                ///< Desired frame rate.
        std::uint32_t  buffer_count { 4u };            ///< Number of V4L2 MMAP buffers to allocate.
    };

    /// @brief Metadata carried on each dequeued V4L2 frame.
    struct frame_metadata
    {
        std::uint32_t sequence {};        ///< Driver-assigned monotonically increasing sequence number.
        std::uint64_t timestamp_ns {};    ///< Kernel capture timestamp (CLOCK_MONOTONIC), nanoseconds.
        std::uint32_t bytes_used {};      ///< Number of valid bytes in the buffer (may be < buffer size).
        std::uint32_t width {};           ///< Actual frame width (after driver negotiation).
        std::uint32_t height {};          ///< Actual frame height (after driver negotiation).
        std::uint32_t fourcc {};          ///< Actual V4L2 FourCC code (after driver negotiation).
    };

} // namespace kmx::aio::readiness::v4l2
