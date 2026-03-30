#pragma once
#include <atomic>
#include <string>
#include <string_view>

#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/readiness/v4l2/capture.hpp>
#include <kmx/aio/task.hpp>
#include <kmx/logger.hpp>

namespace kmx::aio::sample::v4l2::capture
{
    struct config
    {
        std::string                          device        { "/dev/video0" };
        kmx::aio::readiness::v4l2::pixel_format format   { kmx::aio::readiness::v4l2::fourcc::yuyv };
        kmx::aio::readiness::v4l2::frame_size size        { 640u, 480u };
        kmx::aio::readiness::v4l2::frame_rate fps         {};
        std::uint32_t                          buffer_count { 4u };
        std::uint64_t                          max_frames   { 300u }; ///< 0 = unlimited
    };

    struct metrics
    {
        std::atomic_uint64_t frames_captured {};
        std::atomic_uint64_t bytes_captured  {};
        std::atomic_uint64_t errors          {};
    };

    class manager
    {
    public:
        explicit manager(config cfg = {}): config_(std::move(cfg)) {}
        [[nodiscard]] bool run() noexcept(false);

    private:
        [[nodiscard]] kmx::aio::task<void> capture_loop() noexcept(false);
        void print_statistics() const;
        static void signal_handler(int signum) noexcept;

        config  config_;
        metrics metrics_;

        std::shared_ptr<kmx::aio::readiness::executor> executor_;
        static inline std::atomic<kmx::aio::readiness::executor*> g_executor_ptr {};
    };
} // namespace kmx::aio::sample::v4l2::capture
