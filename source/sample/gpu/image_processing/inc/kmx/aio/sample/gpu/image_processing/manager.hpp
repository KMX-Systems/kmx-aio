/// @file inc/kmx/aio/sample/gpu/image_processing/manager.hpp
/// @brief GPU image-processing sample manager and its V4L2 capture and CUDA device configuration.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/completion/v4l2/capture.hpp>

    #include <cstdint>
    #include <string>
    #include <utility>
#endif

namespace kmx::aio::sample::gpu::image_processing
{
    struct config
    {
        std::string device {"/dev/video0"};
        std::uint64_t max_frames {2u};
        kmx::aio::completion::v4l2::pixel_format format {kmx::aio::completion::v4l2::fourcc::yuyv};
        kmx::aio::completion::v4l2::frame_size size {640u, 480u};
        kmx::aio::completion::v4l2::frame_rate fps {};
        std::uint32_t buffer_count {4u};
        std::int16_t gpu_device {0};
    };

    class manager
    {
    public:
        explicit manager(config cfg = {}): config_(std::move(cfg)) {}
        [[nodiscard]] bool run() noexcept;

    private:
        config config_;
    };
}
