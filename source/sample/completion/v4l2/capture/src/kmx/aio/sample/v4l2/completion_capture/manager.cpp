#include <kmx/aio/sample/v4l2/completion_capture/manager.hpp>

#include <chrono>
#include <csignal>
#include <print>
#include <source_location>

#include <kmx/aio/error_code.hpp>

namespace kmx::aio::sample::v4l2::completion_capture
{
    static constexpr auto mem_order = std::memory_order_relaxed;

    bool manager::run() noexcept(false)
    {
        const auto& requested_size = config_.size;
        const auto& requested_fps = config_.fps;

        kmx::logger::log(kmx::logger::level::info, std::source_location::current(),
                         "Opening V4L2 capture device (completion model): {} ({}x{} @ {}/{} fps)", config_.device, requested_size.width,
                         requested_size.height, requested_fps.denominator, requested_fps.numerator);

        kmx::aio::completion::executor_config exec_cfg {
            .ring_entries = 256u,
            .max_completions = 256u,
            .thread_count = 1u,
        };

        executor_ = std::make_unique<kmx::aio::completion::executor>(exec_cfg);
        g_executor_ptr.store(executor_.get(), std::memory_order_release);

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        // Spawn both coroutines into the same completion executor so one io_uring ring
        // drives V4L2 poll events (IORING_OP_POLL_ADD) and periodic timer events
        // (IORING_OP_TIMEOUT) without any epoll instance.
        executor_->spawn(capture_loop());
        executor_->spawn(stats_loop());

        kmx::logger::log(kmx::logger::level::info, std::source_location::current(),
                         "Capture running (completion model). Press Ctrl+C to stop.");
        try
        {
            executor_->run();
        }
        catch (...)
        {
            g_executor_ptr.store(nullptr, std::memory_order_release);
            throw;
        }

        g_executor_ptr.store(nullptr, std::memory_order_release);

        print_statistics();
        return metrics_.errors.load(mem_order) == 0u;
    }

    kmx::aio::task<void> manager::capture_loop() noexcept(false)
    {
        kmx::aio::completion::v4l2::capture_config cfg {
            .device = config_.device,
            .format = config_.format,
            .size = config_.size,
            .fps = config_.fps,
            .buffer_count = config_.buffer_count,
        };

        auto cap_result = kmx::aio::completion::v4l2::capture::create(*executor_, std::move(cfg));
        if (!cap_result)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Failed to open capture device: {}",
                             kmx::aio::to_string(cap_result.error()));
            metrics_.errors.fetch_add(1u, mem_order);
            executor_->stop();
            co_return;
        }

        auto& cap = *cap_result;
        const auto& active_cfg = cap.config();
        const auto& active_size = active_cfg.size;
        const auto& active_format = active_cfg.format;

        kmx::logger::log(kmx::logger::level::info, std::source_location::current(),
                         "Streaming started (completion/io_uring): {}x{} FourCC=0x{:08X}, {} buffers", active_size.width,
                         active_size.height, active_format.fourcc, active_cfg.buffer_count);

        std::uint64_t err_burst {};

        while (true)
        {
            if (config_.max_frames > 0u && metrics_.frames_captured.load(mem_order) >= config_.max_frames)
            {
                kmx::logger::log(kmx::logger::level::info, std::source_location::current(), "Reached max_frames limit ({}). Stopping.",
                                 config_.max_frames);
                break;
            }

            // Suspend via IORING_OP_POLL_ADD; resumed when POLLIN fires on the V4L2 fd.
            auto frame_result = co_await cap.next_frame();
            if (!frame_result)
            {
                metrics_.errors.fetch_add(1u, mem_order);
                kmx::logger::log(kmx::logger::level::warn, std::source_location::current(), "next_frame error: {}",
                                 kmx::aio::to_string(frame_result.error()));
                if (++err_burst > 10u)
                {
                    kmx::logger::log(kmx::logger::level::error, std::source_location::current(),
                                     "Too many consecutive errors. Aborting capture loop.");
                    break;
                }
                continue;
            }

            err_burst = {};
            const auto& frame = *frame_result;
            const auto& meta = frame.metadata();

            metrics_.frames_captured.fetch_add(1u, mem_order);
            metrics_.bytes_captured.fetch_add(meta.bytes_used, mem_order);

            // `frame` destructs here → VIDIOC_QBUF re-enqueues the buffer automatically.
        }

        executor_->stop();
    }

    kmx::aio::task<void> manager::stats_loop() noexcept(false)
    {
        // Demonstrates that completion::timer coexists in the same executor as V4L2 capture.
        // Both use the io_uring ring: capture uses IORING_OP_POLL_ADD, timer uses IORING_OP_TIMEOUT.
        kmx::aio::completion::timer tmr {*executor_};

        while (true)
        {
            const auto wait_res = co_await tmr.wait(std::chrono::seconds(1));
            if (!wait_res)
                co_return; // Executor stopped; exit cleanly.

            const auto frames = metrics_.frames_captured.load(mem_order);
            const auto bytes = metrics_.bytes_captured.load(mem_order);
            const auto errors = metrics_.errors.load(mem_order);

            kmx::logger::log(kmx::logger::level::info, std::source_location::current(),
                             "Stats [io_uring] | frames={} | {:.1f} MiB captured | errors={}", frames,
                             static_cast<double>(bytes) / (1024.0 * 1024.0), errors);
        }
    }

    void manager::print_statistics() const
    {
        std::println("─── V4L2 Completion Capture Statistics");
        std::println("  Execution model : completion (io_uring / IORING_OP_POLL_ADD)");
        std::println("  Frames captured : {}", metrics_.frames_captured.load(mem_order));
        std::println("  Bytes captured  : {} MiB", metrics_.bytes_captured.load(mem_order) / (1024u * 1024u));
        std::println("  Errors          : {}", metrics_.errors.load(mem_order));
        std::println("───────────────────────────────────────────────────────");
    }

    void manager::signal_handler(const int signum) noexcept
    {
        if (auto* exec = g_executor_ptr.load(std::memory_order_acquire); exec != nullptr)
            exec->stop();

        kmx::logger::log(kmx::logger::level::info, std::source_location::current(), "Signal {} received, stopping capture.", signum);
    }

} // namespace kmx::aio::sample::v4l2::completion_capture
