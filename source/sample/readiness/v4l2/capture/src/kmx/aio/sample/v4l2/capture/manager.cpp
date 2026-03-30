#include <kmx/aio/sample/v4l2/capture/manager.hpp>

#include <csignal>
#include <print>
#include <source_location>

#include <kmx/aio/error_code.hpp>

namespace kmx::aio::sample::v4l2::capture
{
    static constexpr auto mem_order = std::memory_order_relaxed;

    bool manager::run() noexcept(false)
    {
        kmx::logger::log(kmx::logger::level::info, std::source_location::current(),
                         "Opening V4L2 capture device: {} ({}x{} @ {}/{} fps)",
                         config_.device, config_.size.width, config_.size.height,
                         config_.fps.denominator, config_.fps.numerator);

        kmx::aio::readiness::executor_config exec_cfg {
            .thread_count = 1u,
            .max_events   = 64u,
            .timeout_ms   = 200u,
        };

        executor_ = std::make_shared<kmx::aio::readiness::executor>(exec_cfg);
        g_executor_ptr.store(executor_.get(), std::memory_order_release);

        std::signal(SIGINT,  signal_handler);
        std::signal(SIGTERM, signal_handler);

        executor_->spawn(capture_loop());

        kmx::logger::log(kmx::logger::level::info, std::source_location::current(),
                         "Capture running. Press Ctrl+C to stop.");
        executor_->run();

        print_statistics();
        return metrics_.errors.load(mem_order) == 0u;
    }

    kmx::aio::task<void> manager::capture_loop() noexcept(false)
    {
        kmx::aio::readiness::v4l2::capture_config cfg {
            .device       = config_.device,
            .format       = config_.format,
            .size         = config_.size,
            .fps          = config_.fps,
            .buffer_count = config_.buffer_count,
        };

        auto cap_result = kmx::aio::readiness::v4l2::capture::create(*executor_, std::move(cfg));
        if (!cap_result)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(),
                             "Failed to open capture device: {}", kmx::aio::to_string(cap_result.error()));
            metrics_.errors.fetch_add(1u, mem_order);
            co_return;
        }

        auto& cap = *cap_result;

        kmx::logger::log(kmx::logger::level::info, std::source_location::current(),
                         "Streaming started: {}x{} FourCC=0x{:08X}, {} buffers",
                         cap.config().size.width, cap.config().size.height,
                         cap.config().format.fourcc, cap.config().buffer_count);

        std::uint64_t err_burst {};

        while (true)
        {
            if (config_.max_frames > 0u &&
                metrics_.frames_captured.load(mem_order) >= config_.max_frames)
            {
                kmx::logger::log(kmx::logger::level::info, std::source_location::current(),
                                 "Reached max_frames limit ({}). Stopping.", config_.max_frames);
                break;
            }

            auto frame_result = co_await cap.next_frame();
            if (!frame_result)
            {
                metrics_.errors.fetch_add(1u, mem_order);
                kmx::logger::log(kmx::logger::level::warn, std::source_location::current(),
                                 "next_frame error: {}", kmx::aio::to_string(frame_result.error()));
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
            const auto& meta  = frame.metadata();

            metrics_.frames_captured.fetch_add(1u, mem_order);
            metrics_.bytes_captured.fetch_add(meta.bytes_used, mem_order);

            // Progress log every 30 frames (~1 second at 30 fps).
            if (const auto n = metrics_.frames_captured.load(mem_order); (n > 0u) && ((n % 30u) == 0u))
            {
                kmx::logger::log(kmx::logger::level::info, std::source_location::current(),
                                 "Frame #{} | seq={} | ts={}ns | {} bytes",
                                 n, meta.sequence, meta.timestamp_ns, meta.bytes_used);
            }

            // `frame` destructs here → VIDIOC_QBUF re-enqueues the buffer automatically.
        }

        executor_->stop();
    }

    void manager::print_statistics() const
    {
        std::println("─── V4L2 Capture Statistics");
        std::println("  Frames captured : {}", metrics_.frames_captured.load(mem_order));
        std::println("  Bytes captured  : {} MiB",
                     metrics_.bytes_captured.load(mem_order) / (1024u * 1024u));
        std::println("  Errors          : {}", metrics_.errors.load(mem_order));
        std::println("───────────────────────────────────────────────────────");
    }

    void manager::signal_handler(const int signum) noexcept
    {
        if (auto* exec = g_executor_ptr.load(std::memory_order_acquire); exec != nullptr)
            exec->stop();

        kmx::logger::log(kmx::logger::level::info, std::source_location::current(),
                         "Signal {} received, stopping capture.", signum);
    }
} // namespace kmx::aio::sample::v4l2::capture
