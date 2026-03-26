#include "kmx/aio/sample/udp/minimal/client/manager.hpp"

#include <sys/socket.h>

namespace kmx::aio::sample::udp::minimal::client
{
    static constexpr auto mem_order = std::memory_order_relaxed;

    bool manager::run() noexcept(false)
    {
        const auto start_time = std::chrono::high_resolution_clock::now();
        const auto server_ip = kmx::aio::ip_to_string(config_.server_addr);

        logger::log(logger::level::info, std::source_location::current(), "Starting async UDP stress test: {} concurrent requests to {}:{}",
                    config_.num_workers, server_ip, config_.server_port);

        // Create executor with thread pool
        const readiness::executor_config exec_config {
            .thread_count = config_.scheduler_threads, .max_events = config_.max_events, .timeout_ms = config_.timeout_ms};

        executor_ = std::make_shared<readiness::executor>(exec_config);

        // Spawn all worker coroutines into the executor
        for (std::uint32_t i {}; i < config_.num_workers; ++i)
            executor_->spawn(worker(i));

        // Run executor (blocks until all tasks complete)
        executor_->run();

        const auto end_time = std::chrono::high_resolution_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        print_summary(elapsed);

        const auto failures = metrics_.failures.load(mem_order);
        logger::log(logger::level::info, std::source_location::current(),
                    "Stress test completed in {} ms with {} successes and {} failures", elapsed.count(), metrics_.successes.load(mem_order),
                    failures);

        return failures == 0u;
    }

    task<void> manager::worker(const std::uint32_t worker_id) noexcept(false)
    {
        metrics_.total_requests.fetch_add(1u, mem_order);

        try
        {
            auto ep_result = kmx::aio::readiness::udp::endpoint::create(*executor_, ip_family(config_.server_addr));
            if (!ep_result)
            {
                logger::log(logger::level::warn, std::source_location::current(), "Worker [{}]: Endpoint creation failed: {}", worker_id,
                            ep_result.error().message());
                metrics_.failures.fetch_add(1u, mem_order);
                metrics_.completed.fetch_add(1u, mem_order);
                co_return;
            }

            auto ep = std::move(*ep_result);
            const auto server_ip = kmx::aio::ip_to_string(config_.server_addr);

            auto server_addr = make_socket_address(config_.server_addr, config_.server_port);
            if (!server_addr)
            {
                logger::log(logger::level::warn, std::source_location::current(), "Worker [{}]: Invalid address format", worker_id);
                metrics_.failures.fetch_add(1u, mem_order);
                metrics_.completed.fetch_add(1u, mem_order);
                co_return;
            }

            // Asynchronously send message
            std::vector<char> message_buffer(config_.message.begin(), config_.message.end());
            const std::span<const std::byte> message_span {reinterpret_cast<const std::byte*>(message_buffer.data()),
                                                           message_buffer.size()};

            if (auto send_result =
                    co_await ep.send(message_span, reinterpret_cast<const sockaddr*>(&server_addr->storage), server_addr->length);
                !send_result)
            {
                logger::log(logger::level::warn, std::source_location::current(), "Worker [{}]: Send failed: {}", worker_id,
                            send_result.error().message());
                metrics_.failures.fetch_add(1u, mem_order);
                metrics_.completed.fetch_add(1u, mem_order);
                co_return;
            }

            logger::log(logger::level::debug, std::source_location::current(), "Worker [{}]: Request sent to {}:{}", worker_id, server_ip,
                        config_.server_port);

            // Asynchronously receive response
            std::vector<std::byte> response_buffer(1024u);
            sockaddr_storage peer_addr {};
            ::socklen_t peer_addr_len = sizeof(peer_addr);

            auto recv_result = co_await ep.recv(response_buffer, peer_addr, peer_addr_len);
            if (!recv_result)
            {
                logger::log(logger::level::warn, std::source_location::current(), "Worker [{}]: Receive failed: {}", worker_id,
                            recv_result.error().message());
                metrics_.failures.fetch_add(1u, mem_order);
                metrics_.completed.fetch_add(1u, mem_order);
                co_return;
            }

            const auto bytes_received = *recv_result;
            logger::log(logger::level::debug, std::source_location::current(), "Worker [{}]: Received {} bytes", worker_id, bytes_received);

            metrics_.successes.fetch_add(1u, mem_order);
        }
        catch (const std::exception& e)
        {
            logger::log(logger::level::error, std::source_location::current(), "Worker [{}]: Exception: {}", worker_id, e.what());
            metrics_.failures.fetch_add(1u, mem_order);
        }

        metrics_.completed.fetch_add(1u, mem_order);
        co_return;
    }

    void manager::print_summary(const std::chrono::milliseconds elapsed) const
    {
        const auto total = metrics_.total_requests.load(mem_order);
        const auto successes = metrics_.successes.load(mem_order);
        const auto failures = metrics_.failures.load(mem_order);
        const auto success_rate = (total > 0) ? ((successes * 100) / total) : 0;

        std::println("\n");
        std::println("╔════════════════════════════════════════╗");
        std::println("║       Client Test Results Summary      ║");
        std::println("╠════════════════════════════════════════╣");
        std::println("║ Total Requests: {:>22} ║", total);
        std::println("║ Successes: {:>27} ║", successes);
        std::println("║ Failures: {:>28} ║", failures);
        std::println("║ Success Rate: {:>23}% ║", success_rate);
        std::println("║ Elapsed Time: {:>21} ms ║", elapsed.count());
        std::println("╚════════════════════════════════════════╝");
    }

} // namespace kmx::aio::sample::udp::minimal::client
