#include <kmx/aio/sample/avb/listener/manager.hpp>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <print>
#include <source_location>

#include <kmx/aio/avb/avtp/am824.hpp>
#include <kmx/aio/completion/avb/eth_socket.hpp>
#include <kmx/aio/completion/timer.hpp>
#include <kmx/aio/sample/avb/manager_model.hpp>
#include <kmx/logger.hpp>

namespace kmx::aio::sample::avb::listener
{
    static constexpr auto mem_order = std::memory_order_relaxed;

    bool manager::run() noexcept(false)
    {
        kmx::logger::log(kmx::logger::level::info, std::source_location::current(), "Starting AVB listener on iface '{}'", config_.iface);

        kmx::aio::completion::executor_config exec_cfg {
            .ring_entries = 256u,
            .max_completions = 256u,
            .thread_count = 1u,
        };

        executor_ = std::make_unique<kmx::aio::completion::executor>(exec_cfg);
        clock_ = std::make_unique<kmx::aio::completion::avb::gptp::clock>(*executor_);
        srp_ = std::make_unique<kmx::aio::completion::avb::srp::client>(*executor_);
        g_executor_ptr.store(executor_.get(), std::memory_order_release);

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        executor_->spawn(receive_loop());
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

    kmx::aio::task<void> manager::receive_loop() noexcept(false)
    {
        const auto clock_start_res = co_await clock_->start(config_.iface);
        if (!clock_start_res)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "gPTP clock start failed: {}",
                             clock_start_res.error().message());
            metrics_.errors.fetch_add(1u, mem_order);
            executor_->stop();
            co_return;
        }

        const auto sync_res = co_await clock_->wait_sync(config_.sync_timeout);
        if (!sync_res)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "gPTP sync failed: {}",
                             sync_res.error().message());
            metrics_.errors.fetch_add(1u, mem_order);
            executor_->stop();
            co_return;
        }

        kmx::aio::completion::avb::eth_socket sock(*executor_);
        const auto open_res = co_await sock.open(config_.iface, kmx::aio::avb::ethertype::avtp);
        if (!open_res)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "AVTP socket open failed: {}",
                             open_res.error().message());
            metrics_.errors.fetch_add(1u, mem_order);
            executor_->stop();
            co_return;
        }

        const auto srp_start = co_await srp_->start(config_.iface);
        if (!srp_start)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SRP start failed: {}",
                             srp_start.error().message());
            metrics_.errors.fetch_add(1u, mem_order);
            executor_->stop();
            co_return;
        }

        kmx::aio::avb::stream_id_t stream_id {};
        stream_id.source_mac = config_.talker_mac;
        stream_id.unique_id = config_.stream_unique_id;

        const auto sub_res = co_await srp_->subscribe(stream_id, config_.srp_subscribe_timeout);
        if (!sub_res)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SRP subscribe failed: {}",
                             sub_res.error().message());
            metrics_.errors.fetch_add(1u, mem_order);
            executor_->stop();
            co_return;
        }

        const auto reserved_stream = sub_res->stream_id;

        if (config_.diagnostics_only)
        {
            kmx::logger::log(kmx::logger::level::info, std::source_location::current(),
                             "Diagnostics-only mode: gPTP sync and SRP subscribe succeeded; skipping AVTP receive loop.");

            const auto srp_withdraw_diag = co_await srp_->withdraw(stream_id);
            if (!srp_withdraw_diag)
                metrics_.errors.fetch_add(1u, mem_order);

            executor_->stop();
            co_return;
        }

        while (metrics_.frames_received.load(mem_order) < config_.max_frames)
        {
            const auto res = co_await sock.recv();
            if (!res)
            {
                metrics_.errors.fetch_add(1u, mem_order);
                continue;
            }

            metrics_.frames_received.fetch_add(1u, mem_order);

            const auto& [frame, rx_ts] = *res;
            const auto parse = kmx::aio::avb::avtp::parse_am824_frame(std::span<const std::byte>(frame));
            if (!parse)
            {
                metrics_.errors.fetch_add(1u, mem_order);
                continue;
            }

            if (parse->stream_id != reserved_stream)
                continue;

            metrics_.frames_parsed.fetch_add(1u, mem_order);

            const auto reference = clock_->now();
            const auto presentation_ts = kmx::aio::avb::avtp::expand_avtp_timestamp_32(parse->avtp_timestamp_32, reference);

            const kmx::aio::avb::avb_timestamp_t abs_jitter = kmx::aio::sample::avb::abs_diff_u64(rx_ts, presentation_ts);
            metrics_.jitter_abs_sum_ns.fetch_add(abs_jitter, mem_order);

            auto cur_max = metrics_.jitter_abs_max_ns.load(mem_order);
            while (abs_jitter > cur_max && !metrics_.jitter_abs_max_ns.compare_exchange_weak(cur_max, abs_jitter, mem_order))
            {
            }
        }

        const auto srp_withdraw = co_await srp_->withdraw(stream_id);
        if (!srp_withdraw)
            metrics_.errors.fetch_add(1u, mem_order);

        executor_->stop();
    }

    kmx::aio::task<void> manager::stats_loop() noexcept(false)
    {
        kmx::aio::completion::timer tmr {*executor_};

        while (true)
        {
            const auto wait_res = co_await tmr.wait(std::chrono::seconds(1));
            if (!wait_res)
                co_return;

            const auto parsed = metrics_.frames_parsed.load(mem_order);
            const auto avg_jitter = (parsed > 1u) ? (metrics_.jitter_abs_sum_ns.load(mem_order) / (parsed - 1u)) : 0u;

            kmx::logger::log(
                kmx::logger::level::info, std::source_location::current(),
                "Listener stats | rx={} parsed={} avg_jitter={}ns max_jitter={}ns errors={} | synced={} | offset={}ns | path_delay={}ns",
                metrics_.frames_received.load(mem_order), parsed, avg_jitter, metrics_.jitter_abs_max_ns.load(mem_order),
                metrics_.errors.load(mem_order), clock_ && clock_->is_synced(), clock_ ? clock_->offset_ns() : 0,
                clock_ ? clock_->path_delay_ns() : 0);
        }
    }

    void manager::print_statistics() const
    {
        const auto parsed = metrics_.frames_parsed.load(mem_order);
        const auto avg_jitter = (parsed > 1u) ? (metrics_.jitter_abs_sum_ns.load(mem_order) / (parsed - 1u)) : 0u;

        std::println("--- AVB Listener Statistics");
        std::println("  Frames received : {}", metrics_.frames_received.load(mem_order));
        std::println("  Frames parsed   : {}", parsed);
        std::println("  Avg jitter (ns) : {}", avg_jitter);
        std::println("  Max jitter (ns) : {}", metrics_.jitter_abs_max_ns.load(mem_order));
        std::println("  Errors          : {}", metrics_.errors.load(mem_order));
        std::println("  Synced          : {}", clock_ && clock_->is_synced());
        std::println("  Offset (ns)     : {}", clock_ ? clock_->offset_ns() : 0);
        std::println("  Path delay      : {}", clock_ ? clock_->path_delay_ns() : 0);
        std::println("--------------------------------");
    }

    void manager::signal_handler(const int signum) noexcept
    {
        if (auto* exec = g_executor_ptr.load(std::memory_order_acquire); exec != nullptr)
            exec->stop();

        kmx::logger::log(kmx::logger::level::info, std::source_location::current(), "Signal {} received, stopping AVB listener.", signum);
    }
} // namespace kmx::aio::sample::avb::listener
