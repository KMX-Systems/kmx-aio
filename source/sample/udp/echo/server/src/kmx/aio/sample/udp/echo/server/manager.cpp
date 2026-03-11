#include <kmx/aio/sample/udp/echo/server/manager.hpp>

#include <csignal>
#include <print>
#include <source_location>
#include <span>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace kmx::aio::sample::udp::echo::server
{
    static constexpr auto mem_order = std::memory_order_relaxed;

    bool manager::run() noexcept(false)
    {
        const auto bind_ip = kmx::aio::ip_to_string(config_.bind_address);
        logger::log(logger::level::info, std::source_location::current(), "Starting UDP Echo Server on {}:{}", bind_ip,
                    config_.bind_port);

        executor_config exec_cfg {
            .thread_count = config_.executor_threads, .max_events = config_.max_events, .timeout_ms = config_.timeout_ms};

        executor_ = std::make_shared<executor>(exec_cfg);
        g_executor_ptr.store(executor_.get(), std::memory_order_release);

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        for (std::uint32_t i = 0; i < config_.listener_workers; ++i)
        {
            executor_->spawn(listener(i));
        }

        logger::log(logger::level::info, std::source_location::current(), "Server running. Press Ctrl+C to stop.");
        executor_->run();

        print_statistics();
        return metrics_.errors.load(mem_order) == 0;
    }

    task<void> manager::listener(const std::uint32_t worker_id) noexcept(false)
    {
        try
        {
            auto ep_result = kmx::aio::udp::endpoint::create(*executor_, ip_family(config_.bind_address));
            if (!ep_result)
            {
                logger::log(logger::level::error, std::source_location::current(), "Worker [{}]: Failed to create endpoint: {}", worker_id,
                            ep_result.error().message());
                co_return;
            }

            auto ep = std::move(*ep_result);
            int opt = 1;
            if (::setsockopt(ep.raw().get_fd(), SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
            {
                logger::log(logger::level::error, std::source_location::current(), "Worker [{}]: setsockopt SO_REUSEPORT failed: {}",
                            worker_id, std::strerror(errno));
                co_return;
            }

            auto addr = make_socket_address(config_.bind_address, config_.bind_port);
            if (!addr)
            {
                logger::log(logger::level::error, std::source_location::current(), "Worker [{}]: Invalid bind address.", worker_id);
                co_return;
            }

            if (::bind(ep.raw().get_fd(), reinterpret_cast<sockaddr*>(&addr->storage), addr->length) < 0)
            {
                logger::log(logger::level::error, std::source_location::current(), "Worker [{}]: bind failed: {}", worker_id,
                            std::strerror(errno));
                co_return;
            }

            logger::log(logger::level::debug, std::source_location::current(), "Worker [{}]: Ready to receive.", worker_id);

            std::vector<std::byte> buffer(65535u);
            std::uint64_t err_burst = 0;

            while (true)
            {
                sockaddr_storage peer {};
                ::socklen_t peer_len = sizeof(peer);

                auto recv_result = co_await ep.recv(buffer, peer, peer_len);
                if (!recv_result)
                {
                    metrics_.errors.fetch_add(1u, mem_order);
                    logger::log(logger::level::warn, std::source_location::current(), "Worker [{}]: recv error: {}", worker_id,
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

                std::span<const std::byte> send_buf {buffer.data(), (std::size_t) bytes_recv};
                auto send_result = co_await ep.send(send_buf, reinterpret_cast<const sockaddr*>(&peer), peer_len);

                if (auto total = metrics_.messages_handled.load(mem_order); total > 0 && total % 1000 == 0)
                {
                    logger::log(logger::level::info, std::source_location::current(), "Server: Handled {} messages so far.", total);
                }

                if (!send_result)
                {
                    metrics_.errors.fetch_add(1u, mem_order);
                    logger::log(logger::level::warn, std::source_location::current(), "Worker [{}]: send error: {}", worker_id,
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

    void manager::print_statistics() const
    {
        const auto msgs = metrics_.messages_handled.load(mem_order);
        const auto b_recv = metrics_.bytes_received.load(mem_order);
        const auto b_sent = metrics_.bytes_sent.load(mem_order);
        const auto errs = metrics_.errors.load(mem_order);

        std::println("\n╔════════════════════════════════════════════════════════╗");
        std::println("║         UDP Echo Server Final Statistics               ║");
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
            const char msg[] = "\n[SIGNAL] Stopping UDP Echo Server executor...\n";
            [[maybe_unused]] auto res = ::write(STDERR_FILENO, msg, sizeof(msg) - 1);

            auto* exec = g_executor_ptr.load(std::memory_order_acquire);
            if (exec)
                exec->stop();
        }
    }
}
