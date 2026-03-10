#include <kmx/aio/sample/udp/echo/client/manager.hpp>

#include <sys/socket.h>

#include <print>
#include <source_location>
#include <span>
#include <vector>

namespace kmx::aio::sample::udp::echo::client
{
    static constexpr auto mem_order = std::memory_order_relaxed;

    bool manager::run() noexcept(false)
    {
        auto start_time = std::chrono::high_resolution_clock::now();
        const auto server_ip = kmx::aio::ip_to_string(config_.server_address);
        logger::log(logger::level::info, std::source_location::current(),
                    "Starting UDP Echo Client: {} concurrent workers, {} messages/worker to {}:{}", config_.concurrency,
                config_.messages_per_worker, server_ip, config_.server_port);

        executor_config exec_cfg {
            .thread_count = config_.executor_threads, .max_events = config_.max_events, .timeout_ms = config_.timeout_ms};

        executor_ = std::make_shared<executor>(exec_cfg);

        for (std::uint32_t i = 0; i < config_.concurrency; ++i)
        {
            executor_->spawn(worker(i));
        }

        executor_->run();

        auto end_time = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        print_statistics(elapsed);

        return metrics_.errors.load(mem_order) == 0;
    }

    task<void> manager::worker(const std::uint32_t worker_id) noexcept(false)
    {
        try
        {
            auto ep_result = kmx::aio::udp::endpoint::create(*executor_, ip_family(config_.server_address));
            if (!ep_result)
            {
                logger::log(logger::level::warn, std::source_location::current(), "Worker [{}]: Failed to create UDP endpoint: {}",
                            worker_id, ep_result.error().message());
                metrics_.errors.fetch_add(1u, mem_order);
                metrics_.completed_workers.fetch_add(1u, mem_order);
                co_return;
            }

            auto ep = std::move(*ep_result);
            auto server_addr = make_socket_address(config_.server_address, config_.server_port);
            if (!server_addr)
            {
                logger::log(logger::level::error, std::source_location::current(), "Worker [{}]: Invalid server address.", worker_id);
                metrics_.errors.fetch_add(1u, mem_order);
                metrics_.completed_workers.fetch_add(1u, mem_order);
                co_return;
            }

            std::vector<char> message_buf(config_.payload.begin(), config_.payload.end());
            std::span<const std::byte> send_buf {reinterpret_cast<const std::byte*>(message_buf.data()), message_buf.size()};

            std::vector<std::byte> recv_buf(4096u);

            for (std::uint32_t msg = 0; msg < config_.messages_per_worker; ++msg)
            {
                if (msg > 0 && msg % 100 == 0)
                {
                    logger::log(logger::level::info, std::source_location::current(), "Worker [{}]: Sent {} messages so far", worker_id,
                                msg);
                }
                auto send_result = co_await ep.send(send_buf, reinterpret_cast<const sockaddr*>(&server_addr->storage),
                                                    server_addr->length);
                if (!send_result)
                {
                    logger::log(logger::level::warn, std::source_location::current(), "Worker [{}]: Send error: {}", worker_id,
                                send_result.error().message());
                    metrics_.errors.fetch_add(1u, mem_order);
                    break;
                }

                metrics_.messages_sent.fetch_add(1u, mem_order);
                metrics_.bytes_sent.fetch_add(*send_result, mem_order);

                sockaddr_storage peer {};
                socklen_t peer_len = sizeof(peer);
                auto recv_result = co_await ep.recv(recv_buf, peer, peer_len);
                if (!recv_result)
                {
                    logger::log(logger::level::warn, std::source_location::current(), "Worker [{}]: Receive error: {}", worker_id,
                                recv_result.error().message());
                    metrics_.errors.fetch_add(1u, mem_order);
                    break;
                }

                metrics_.messages_received.fetch_add(1u, mem_order);
                metrics_.bytes_received.fetch_add(*recv_result, mem_order);
            }
        }
        catch (const std::exception& e)
        {
            logger::log(logger::level::error, std::source_location::current(), "Worker [{}]: Exception: {}", worker_id, e.what());
            metrics_.errors.fetch_add(1u, mem_order);
        }

        metrics_.completed_workers.fetch_add(1u, mem_order);
        co_return;
    }

    void manager::print_statistics(std::chrono::milliseconds elapsed) const
    {
        const auto sent = metrics_.messages_sent.load(mem_order);
        const auto received = metrics_.messages_received.load(mem_order);
        const auto errs = metrics_.errors.load(mem_order);
        const auto bsent = metrics_.bytes_sent.load(mem_order);
        const auto brecv = metrics_.bytes_received.load(mem_order);

        std::println("\n╔════════════════════════════════════════════════════════╗");
        std::println("║         UDP Echo Client Results                        ║");
        std::println("╠════════════════════════════════════════════════════════╣");
        std::println("║ Elapsed Time:      {:>25} ms        ║", elapsed.count());
        std::println("║ Messages Sent:     {:>32}    ║", sent);
        std::println("║ Messages Received: {:>32}    ║", received);
        std::println("║ Bytes Sent:        {:>32}    ║", bsent);
        std::println("║ Bytes Received:    {:>32}    ║", brecv);
        std::println("║ Errors:            {:>32}    ║", errs);
        std::println("╚════════════════════════════════════════════════════════╝\n");
    }
}
