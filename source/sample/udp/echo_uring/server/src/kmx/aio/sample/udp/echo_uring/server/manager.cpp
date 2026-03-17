#include "kmx/aio/sample/udp/echo_uring/server/manager.hpp"

#include <chrono>
#include <print>
#include <source_location>
#include <span>
#include <sys/socket.h>

namespace kmx::aio::sample::udp::echo_uring::server
{
    static constexpr auto mem_order = std::memory_order_relaxed;

    bool manager::run() noexcept(false)
    {
        const auto bind_ip = kmx::aio::ip_to_string(config_.bind_address);
        logger::log(logger::level::info, std::source_location::current(), "Starting UDP Uring Echo Server on {}:{}", bind_ip,
                    config_.bind_port);

        kmx::aio::completion::executor_config exec_cfg {
            .ring_entries = config_.max_events,
            .max_completions = config_.max_events,
            .thread_count = config_.executor_threads,
            .core_id = -1
        };

        executor_ = std::make_shared<kmx::aio::completion::executor>(exec_cfg);
        g_executor_ptr.store(executor_.get(), std::memory_order_release);

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        for (std::uint32_t i = 0; i < config_.listener_workers; ++i)
        {
            executor_->spawn(listener(i));
        }

        ui_thread_ = std::jthread([this](std::stop_token stop_token) { ui_loop(stop_token); });

        logger::log(logger::level::info, std::source_location::current(), "Server running. Press Ctrl+C to stop.");
        executor_->run();

        if (ui_thread_.joinable())
        {
            ui_thread_.request_stop();
            ui_thread_.join();
        }

        print_statistics();
        return metrics_.errors.load(mem_order) == 0;
    }

    task<void> manager::listener(const std::uint32_t worker_id) noexcept(false)
    {
        try
        {
            auto sock_result = kmx::aio::completion::udp::socket::create(executor_, ip_family(config_.bind_address));
            if (!sock_result)
            {
                logger::log(logger::level::error, std::source_location::current(), "Worker [{}]: Failed to create socket: {}", worker_id,
                            sock_result.error().message());
                co_return;
            }

            auto sock = std::move(*sock_result);
            int opt = 1;
            if (::setsockopt(sock.get_fd(), SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
            {
                logger::log(logger::level::error, std::source_location::current(), "Worker [{}]: setsockopt SO_REUSEPORT failed: {}",
                            worker_id, std::strerror(errno));
                co_return;
            }

            if (auto bind_res = sock.bind(config_.bind_address, config_.bind_port); !bind_res)
            {
                logger::log(logger::level::error, std::source_location::current(), "Worker [{}]: bind failed: {}", worker_id,
                            bind_res.error().message());
                co_return;
            }

            logger::log(logger::level::debug, std::source_location::current(), "Worker [{}]: Ready to receive.", worker_id);

            std::vector<std::byte> buffer(65535u);
            std::uint64_t err_burst = 0;

            while (true)
            {
                sockaddr_storage peer {};

                ::iovec iov;
                iov.iov_base = buffer.data();
                iov.iov_len = buffer.size();

                ::msghdr msg {};
                msg.msg_name = &peer;
                msg.msg_namelen = sizeof(peer);
                msg.msg_iov = &iov;
                msg.msg_iovlen = 1;

                auto recv_result = co_await sock.recvmsg(&msg, 0);
                if (!recv_result)
                {
                    metrics_.errors.fetch_add(1u, mem_order);
                    logger::log(logger::level::warn, std::source_location::current(), "Worker [{}]: recvmsg error: {}", worker_id,
                                recv_result.error().message());
                    if (++err_burst > 100)
                    {
                        logger::log(logger::level::error, std::source_location::current(),
                                    "Worker [{}]: Breaking loop due to too many errors.", worker_id);
                        break;
                    }
                    continue;
                }

                err_burst = 0;
                auto bytes_recv = *recv_result;
                metrics_.bytes_received.fetch_add(bytes_recv, mem_order);

                msg.msg_namelen = sizeof(peer); // It could be overwritten by recvmsg
                iov.iov_len = bytes_recv;

                auto send_result = co_await sock.sendmsg(&msg, 0);

                if (!send_result)
                {
                    metrics_.errors.fetch_add(1u, mem_order);
                    logger::log(logger::level::warn, std::source_location::current(), "Worker [{}]: sendmsg error: {}", worker_id,
                                send_result.error().message());
                }
                else
                {
                    metrics_.bytes_sent.fetch_add(*send_result, mem_order);
                    metrics_.messages_handled.fetch_add(1u, mem_order);
                }
            }
        }
        catch (const std::exception& e)
        {
            logger::log(logger::level::error, std::source_location::current(), "Worker [{}]: Exception: {}", worker_id, e.what());
        }

        co_return;
    }

    void manager::ui_loop(std::stop_token stop_token) const
    {
        using namespace std::chrono_literals;

        while (!stop_token.stop_requested())
        {
            const auto msgs = metrics_.messages_handled.load(mem_order);
            const auto b_recv = metrics_.bytes_received.load(mem_order);
            const auto b_sent = metrics_.bytes_sent.load(mem_order);
            const auto errs = metrics_.errors.load(mem_order);

            std::cout << "\x1b[2J\x1b[H";
            std::cout << "Live UDP Uring Server Stats\n";
            std::cout << "────────────────────────────────────────────────────────────────────────\n";
            std::cout << std::format("Server Totals: TX {:>10} | RX {:>10} | Msgs: {} | Errors: {}\n", b_sent, b_recv, msgs, errs);
            std::cout << std::flush;

            std::this_thread::sleep_for(250ms);
        }
    }

    void manager::print_statistics() const
    {
        const auto msgs = metrics_.messages_handled.load(mem_order);
        const auto b_recv = metrics_.bytes_received.load(mem_order);
        const auto b_sent = metrics_.bytes_sent.load(mem_order);
        const auto errs = metrics_.errors.load(mem_order);

        std::println("\n╔════════════════════════════════════════════════════════╗");
        std::println("║      UDP Uring Echo Server Final Statistics            ║");
        std::println("╠════════════════════════════════════════════════════════╣");
        std::println("║ Messages Handled:  {:>32}    ║", msgs);
        std::println("║ Bytes Received:    {:>32}    ║", b_recv);
        std::println("║ Bytes Sent:        {:>32}    ║", b_sent);
        std::println("║ Errors:            {:>32}    ║", errs);
        std::println("╚════════════════════════════════════════════════════════╝\n");
    }

    void manager::signal_handler(int signum) noexcept
    {
        if (signum == SIGINT || signum == SIGTERM)
        {
            const char msg[] = "\n[SIGNAL] Stopping UDP Uring Server executor...\n";
            [[maybe_unused]] auto res = ::write(STDERR_FILENO, msg, sizeof(msg) - 1);

            auto* exec = g_executor_ptr.load(std::memory_order_acquire);
            if (exec)
                exec->stop();
        }
    }
}
