#include <kmx/aio/sample/xdp/packet_filter/manager.hpp>

#include <atomic>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <memory>
#include <source_location>
#include <string>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/xdp/socket.hpp>
#include <kmx/aio/task.hpp>
#include <kmx/logger.hpp>

namespace kmx::aio::sample::xdp::packet_filter
{
    void log_xdp_setup_hints(const std::string& interface_name, std::uint32_t queue_id)
    {
        kmx::logger::log(kmx::logger::level::info, std::source_location::current(),
                         "Hint: ensure CAP_NET_ADMIN/CAP_BPF (or run as root), then verify interface '{}' queue {} exists", interface_name,
                         queue_id);

        const auto iface_path = std::filesystem::path("/sys/class/net") / interface_name;
        if (!std::filesystem::exists(iface_path))
        {
            kmx::logger::log(kmx::logger::level::info, std::source_location::current(),
                             "Hint: interface '{}' is not present under /sys/class/net", interface_name);
        }

        kmx::logger::log(kmx::logger::level::info, std::source_location::current(),
                         "Hint: if XDP program attach is blocked, check driver/offload support and kernel logs via 'dmesg | tail -n 50'");
    }

    auto run_packet_filter(std::shared_ptr<kmx::aio::completion::executor> exec, std::shared_ptr<std::atomic_bool> ok,
                           std::string interface_name, std::uint32_t queue_id) -> kmx::aio::task<void>
    {
        kmx::aio::completion::xdp::socket_config cfg {
            .interface_name = interface_name,
            .queue_id = queue_id,
        };

        auto sock_result = kmx::aio::completion::xdp::socket::create(exec, cfg);
        if (!sock_result)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "AF_XDP socket create failed: {}",
                             sock_result.error().message());
            log_xdp_setup_hints(interface_name, queue_id);
            exec->stop();
            co_return;
        }

        auto sock = std::move(*sock_result);

        kmx::logger::log(kmx::logger::level::info, std::source_location::current(), "Listening on {}, starting receive loop...",
                         interface_name);

        for (int i{}; i < 50; ++i)
        {
            auto recv_result = co_await sock.recv();
            if (!recv_result)
            {
                if (recv_result.error() == std::make_error_code(std::errc::operation_would_block))
                {
                    sock.trigger_wakeup();
                    continue;
                }

                kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Receive failed: {}",
                                 recv_result.error().message());
                break;
            }

            auto& frame = *recv_result;
            // Mock packet processing drop logic
            sock.release_frame(frame.addr);
        }

        const auto& stats = sock.get_stats();
        kmx::logger::log(kmx::logger::level::info, std::source_location::current(),
                         "Metrics: User_Rx={} User_Tx={} Krnl_Dropps={} Krnl_RingFull={} Wakeups={}", stats.rx_frames_received,
                         stats.tx_frames_sent, stats.kernel_rx_dropped, stats.kernel_rx_ring_full, stats.wakeups_triggered);

        ok->store(true, std::memory_order_relaxed);
        exec->stop();
    }
}
