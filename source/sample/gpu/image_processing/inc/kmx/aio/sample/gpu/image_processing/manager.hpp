#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include <kmx/aio/completion/v4l2/capture.hpp>

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
