#include <kmx/aio/sample/avb/talker/manager.hpp>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <print>
#include <source_location>
#include <vector>

#include <kmx/aio/avb/avtp/am824.hpp>
#include <kmx/aio/readiness/avb/eth_socket.hpp>
#include <kmx/logger.hpp>

namespace kmx::aio::sample::avb::talker
{
    static constexpr auto mem_order = std::memory_order_relaxed;

    bool manager::run() noexcept(false)
    {
        kmx::logger::log(kmx::logger::level::info, std::source_location::current(), "Starting AVB talker on iface '{}'", config_.iface);

        kmx::aio::readiness::executor_config exec_cfg {
            .thread_count = 1u,
            .max_events = 1024u,
            .timeout_ms = 200u,
        };

        executor_ = std::make_shared<kmx::aio::readiness::executor>(exec_cfg);
        clock_ = std::make_unique<kmx::aio::readiness::avb::gptp::clock>(*executor_);
        srp_ = std::make_unique<kmx::aio::readiness::avb::srp::client>(*executor_);
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

        kmx::aio::readiness::avb::eth_socket sock(*executor_);
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

        kmx::aio::avb::srp::stream_descriptor desc {};
        desc.stream_id = stream_id;
        desc.dest_mac = config_.dest_mac;
        desc.max_frame_size = static_cast<std::uint16_t>(std::min<std::uint32_t>(config_.payload_bytes, 0xFFFFu));
        desc.max_interval_frames = 1u;
        desc.priority_and_rank = 0x60u;
        desc.accumulated_latency = 2'000'000u;

        const auto srp_start = co_await srp_->start(config_.iface);
        if (!srp_start)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SRP start failed: {}",
                             srp_start.error().message());
            metrics_.errors.fetch_add(1u, mem_order);
            executor_->stop();
            co_return;
        }

        const auto srp_adv = co_await srp_->advertise(desc);
        if (!srp_adv)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SRP advertise failed: {}",
                             srp_adv.error().message());
            metrics_.errors.fetch_add(1u, mem_order);
            executor_->stop();
            co_return;
        }

        if (config_.diagnostics_only)
        {
            kmx::logger::log(kmx::logger::level::info, std::source_location::current(),
                             "Diagnostics-only mode: gPTP sync and SRP advertise succeeded; skipping AVTP payload send loop.");

            const auto srp_withdraw_diag = co_await srp_->withdraw(stream_id);
            if (!srp_withdraw_diag)
                metrics_.errors.fetch_add(1u, mem_order);

            executor_->stop();
            co_return;
        }

        std::vector<std::byte> payload(config_.payload_bytes, std::byte {0});
        std::uint8_t seq = 0u;

        for (std::uint64_t i = 0u; i < config_.max_frames; ++i)
        {
            for (std::size_t j = 0; j < payload.size(); ++j)
                payload[j] = static_cast<std::byte>((i + j) & 0xFFu);

            const auto now = clock_->now();
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

            const auto wait_ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(config_.frame_period).count());
            const auto wait_res = co_await executor_->async_timeout(wait_ns);
            if (!wait_res)
                break;
        }

        const auto srp_withdraw = co_await srp_->withdraw(stream_id);
        if (!srp_withdraw)
            metrics_.errors.fetch_add(1u, mem_order);

        executor_->stop();
    }

    kmx::aio::task<void> manager::stats_loop() noexcept(false)
    {
        while (true)
        {
            const auto wait_res = co_await executor_->async_timeout(1'000'000'000ULL);
            if (!wait_res)
                co_return;

            kmx::logger::log(kmx::logger::level::info, std::source_location::current(),
                             "Talker stats | frames_sent={} | errors={} | synced={} | offset={}ns | path_delay={}ns",
                             metrics_.frames_sent.load(mem_order), metrics_.errors.load(mem_order), clock_ && clock_->is_synced(),
                             clock_ ? clock_->offset_ns() : 0, clock_ ? clock_->path_delay_ns() : 0);
        }
    }

    void manager::print_statistics() const
    {
        std::println("--- AVB Talker Statistics");
        std::println("  Frames sent : {}", metrics_.frames_sent.load(mem_order));
        std::println("  Errors      : {}", metrics_.errors.load(mem_order));
        std::println("  Synced      : {}", clock_ && clock_->is_synced());
        std::println("  Offset (ns) : {}", clock_ ? clock_->offset_ns() : 0);
        std::println("  Path delay  : {}", clock_ ? clock_->path_delay_ns() : 0);
        std::println("------------------------------");
    }

    void manager::signal_handler(const int signum) noexcept
    {
        if (auto* exec = g_executor_ptr.load(std::memory_order_acquire); exec != nullptr)
            exec->stop();

        kmx::logger::log(kmx::logger::level::info, std::source_location::current(), "Signal {} received, stopping AVB talker.", signum);
    }
} // namespace kmx::aio::sample::avb::talker
