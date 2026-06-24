#include <kmx/aio/sample/avb/talker/manager.hpp>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <ctime>
#include <print>
#include <source_location>
#include <vector>

#include <kmx/aio/avb/avtp/am824.hpp>
#include <kmx/aio/completion/avb/eth_socket.hpp>
#include <kmx/aio/completion/timer.hpp>
#include <kmx/logger.hpp>

namespace kmx::aio::sample::avb::talker
{
    static constexpr auto mem_order = std::memory_order_relaxed;

    [[nodiscard]] static kmx::aio::avb::avb_timestamp_t clock_tai_now_ns() noexcept
    {
        ::timespec ts {};
        ::clock_gettime(CLOCK_TAI, &ts);
        return static_cast<kmx::aio::avb::avb_timestamp_t>(ts.tv_sec) * 1'000'000'000ULL +
               static_cast<kmx::aio::avb::avb_timestamp_t>(ts.tv_nsec);
    }

    bool manager::run() noexcept(false)
    {
        kmx::logger::log(kmx::logger::level::info, std::source_location::current(), "Starting AVB talker on iface '{}'", config_.iface);

        kmx::aio::completion::executor_config exec_cfg {
            .ring_entries = 256u,
            .max_completions = 256u,
            .thread_count = 1u,
        };

        executor_ = std::make_shared<kmx::aio::completion::executor>(exec_cfg);
        g_executor_ptr.store(executor_.get(), std::memory_order_release);

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        executor_->spawn(talker_loop());
        executor_->spawn(stats_loop());

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

    kmx::aio::task<void> manager::talker_loop() noexcept(false)
    {
        kmx::aio::completion::avb::eth_socket sock(*executor_);
        auto open_res = co_await sock.open(config_.iface, kmx::aio::avb::ethertype::avtp);
        if (!open_res)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "AVTP socket open failed: {}",
                             open_res.error().message());
            metrics_.errors.fetch_add(1u, mem_order);
            executor_->stop();
            co_return;
        }

        kmx::aio::avb::stream_id_t stream_id {};
        stream_id.source_mac = sock.local_mac();
        stream_id.unique_id = config_.stream_unique_id;

        kmx::aio::completion::timer tick {executor_};

        std::vector<std::byte> payload(config_.payload_bytes, std::byte {0});
        std::uint8_t seq = 0u;

        for (std::uint64_t i = 0u; i < config_.max_frames; ++i)
        {
            for (std::size_t j = 0; j < payload.size(); ++j)
                payload[j] = static_cast<std::byte>((i + j) & 0xFFu);

            const auto now = clock_tai_now_ns();
            const auto presentation_ns = now + 2'000'000ULL;
            const auto frame_res =
                kmx::aio::avb::avtp::build_am824_frame(stream_id, seq++, presentation_ns, std::span<const std::byte>(payload));
            if (!frame_res)
            {
                metrics_.errors.fetch_add(1u, mem_order);
                continue;
            }

            const auto send_res = co_await sock.send(config_.dest_mac, std::span<const std::byte>(*frame_res), presentation_ns);
            if (!send_res)
            {
                metrics_.errors.fetch_add(1u, mem_order);
            }
            else
            {
                metrics_.frames_sent.fetch_add(1u, mem_order);
            }

            const auto wait_res = co_await tick.wait(config_.frame_period);
            if (!wait_res)
                break;
        }

        executor_->stop();
    }

    kmx::aio::task<void> manager::stats_loop() noexcept(false)
    {
        kmx::aio::completion::timer tmr {executor_};

        while (true)
        {
            const auto wait_res = co_await tmr.wait(std::chrono::seconds(1));
            if (!wait_res)
                co_return;

            kmx::logger::log(kmx::logger::level::info, std::source_location::current(), "Talker stats | frames_sent={} | errors={}",
                             metrics_.frames_sent.load(mem_order), metrics_.errors.load(mem_order));
        }
    }

    void manager::print_statistics() const
    {
        std::println("--- AVB Talker Statistics");
        std::println("  Frames sent : {}", metrics_.frames_sent.load(mem_order));
        std::println("  Errors      : {}", metrics_.errors.load(mem_order));
        std::println("------------------------------");
    }

    void manager::signal_handler(const int signum) noexcept
    {
        if (auto* exec = g_executor_ptr.load(std::memory_order_acquire); exec != nullptr)
            exec->stop();

        kmx::logger::log(kmx::logger::level::info, std::source_location::current(), "Signal {} received, stopping AVB talker.", signum);
    }
} // namespace kmx::aio::sample::avb::talker
