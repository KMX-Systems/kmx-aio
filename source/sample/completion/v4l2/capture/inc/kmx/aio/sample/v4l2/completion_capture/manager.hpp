/// @file kmx/aio/sample/v4l2/completion_capture/manager.hpp
/// @brief Completion-model V4L2 capture sample manager.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <atomic>
#include <memory>
#include <string>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/timer.hpp>
#include <kmx/aio/completion/v4l2/capture.hpp>
#include <kmx/aio/task.hpp>
#include <kmx/logger.hpp>

namespace kmx::aio::sample::v4l2::completion_capture
{
    struct config
    {
        std::string                                 device        { "/dev/video0" };
        kmx::aio::completion::v4l2::pixel_format    format        { kmx::aio::completion::v4l2::fourcc::yuyv };
        kmx::aio::completion::v4l2::frame_size      size          { 640u, 480u };
        kmx::aio::completion::v4l2::frame_rate      fps           {};
        std::uint32_t                               buffer_count  { 4u };
        std::uint64_t                               max_frames    { 300u }; ///< 0 = unlimited
    };

    struct metrics
    {
        std::atomic_uint64_t frames_captured {};
        std::atomic_uint64_t bytes_captured  {};
        std::atomic_uint64_t errors          {};
    };

    /// @brief Drives V4L2 capture and a periodic stats timer under one completion executor.
    ///
    /// Demonstrates the core value of the completion model: one `completion::executor`
    /// simultaneously drives `completion::v4l2::capture` (via `IORING_OP_POLL_ADD`) and
    /// a `completion::timer` (via `IORING_OP_TIMEOUT`) in the same io_uring ring, with
    /// no separate epoll instance or readiness executor required.
    class manager
    {
    public:
        explicit manager(config cfg = {}): config_(std::move(cfg)) {}
        [[nodiscard]] bool run() noexcept(false);

    private:
        [[nodiscard]] kmx::aio::task<void> capture_loop() noexcept(false);
        [[nodiscard]] kmx::aio::task<void> stats_loop() noexcept(false);
        void print_statistics() const;
        static void signal_handler(int signum) noexcept;

        config  config_;
        metrics metrics_;

        std::shared_ptr<kmx::aio::completion::executor> executor_;
        static inline std::atomic<kmx::aio::completion::executor*> g_executor_ptr {};
    };

} // namespace kmx::aio::sample::v4l2::completion_capture
