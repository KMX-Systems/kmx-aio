#include "kmx/aio/sample/udp/minimal/server/manager.hpp"

#include <sys/socket.h>

namespace kmx::aio::sample::udp::minimal::server
{
    bool manager::run() noexcept(false)
    {
        logger::log(logger::level::info, std::source_location::current(),
                    "═══════════════════════════════════════════════════════════════");
        logger::log(logger::level::info, std::source_location::current(), "UDP Echo Server Starting");
        logger::log(logger::level::info, std::source_location::current(),
                    "═══════════════════════════════════════════════════════════════");

        // Configure executor
        const executor_config exec_config {
            .thread_count = config_.scheduler_threads, .max_events = config_.epoll_max_events, .timeout_ms = config_.epoll_timeout_ms};

        logger::log(logger::level::info, std::source_location::current(), "Server: Initializing executor with {} scheduler threads",
                    config_.scheduler_threads);
        logger::log(logger::level::debug, std::source_location::current(), "Server: Epoll config - max_events: {}, timeout: {} ms",
                    config_.epoll_max_events, config_.epoll_timeout_ms);

        executor_ = std::make_shared<executor>(exec_config);
        g_executor_ptr.store(executor_.get(), std::memory_order_release);

        logger::log(logger::level::info, std::source_location::current(), "Server: Executor created successfully");

        // Register signal handlers
        logger::log(logger::level::debug, std::source_location::current(), "Server: Registering signal handlers (SIGINT, SIGTERM)");

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        logger::log(logger::level::info, std::source_location::current(), "Server: Signal handlers registered");

        // Spawn the main loop task
        logger::log(logger::level::info, std::source_location::current(), "Server: Spawning server loop coroutine");

        auto loop_task = server_loop();
        executor_->spawn(std::move(loop_task));

        const auto bind_ip = kmx::aio::ip_to_string(config_.bind_address);

        logger::log(logger::level::info, std::source_location::current(),
                    "═══════════════════════════════════════════════════════════════");
        logger::log(logger::level::info, std::source_location::current(), "Server: Started. Bound to {}:{}", bind_ip,
                    config_.bind_port);
        logger::log(logger::level::info, std::source_location::current(), "Server: Press Ctrl+C to shutdown");
        logger::log(logger::level::info, std::source_location::current(),
                    "═══════════════════════════════════════════════════════════════");

        // Run the event loop (blocks until stop() is called)
        logger::log(logger::level::info, std::source_location::current(), "Server: Running event loop");

        executor_->run();

        logger::log(logger::level::info, std::source_location::current(), "Server: Event loop stopped");

        print_statistics();

        logger::log(logger::level::info, std::source_location::current(),
                    "═══════════════════════════════════════════════════════════════");
        logger::log(logger::level::info, std::source_location::current(), "Server: Shutdown complete");
        logger::log(logger::level::info, std::source_location::current(),
                    "═══════════════════════════════════════════════════════════════");

        return metrics_.errors == 0u;
    }

    kmx::aio::task<void> manager::server_loop() noexcept(false)
    {
        // Create UDP endpoint
        const auto bind_ip = kmx::aio::ip_to_string(config_.bind_address);
        logger::log(logger::level::info, std::source_location::current(), "Server: Creating UDP endpoint on {}:{}", bind_ip,
                    config_.bind_port);

        auto ep_result = kmx::aio::udp::endpoint::create(*executor_, ip_family(config_.bind_address));

        if (!ep_result)
        {
            logger::log(logger::level::error, std::source_location::current(), "Server: Failed to create endpoint: {}",
                        ep_result.error().message());
            co_return;
        }

        auto ep = std::move(*ep_result);

        // Bind address
        auto addr = make_socket_address(config_.bind_address, config_.bind_port);
        if (!addr)
        {
            logger::log(logger::level::error, std::source_location::current(), "Server: Invalid bind address format");
            co_return;
        }

        if (::bind(ep.raw().get_fd(), reinterpret_cast<sockaddr*>(&addr->storage), addr->length) < 0)
        {
            logger::log(logger::level::error, std::source_location::current(), "Server: Bind failed: {}", std::strerror(errno));
            co_return;
        }

        logger::log(logger::level::info, std::source_location::current(), "Server: Bound successfully");

        std::uint64_t msg_errors = 0;

        try
        {
            std::vector<std::byte> buffer(4096u);
            while (true)
            {
                logger::log(logger::level::debug, std::source_location::current(), "Server: Waiting for datagrams...");

                sockaddr_storage peer_addr {};
                ::socklen_t peer_addr_len = sizeof(peer_addr);

                auto recv_result = co_await ep.recv(buffer, peer_addr, peer_addr_len);

                if (!recv_result)
                {
                    logger::log(logger::level::warn, std::source_location::current(), "Server: Receive error: {}",
                                recv_result.error().message());
                    metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
                    ++msg_errors;

                    // Prevent tight loop on error
                    if (msg_errors > 10)
                    {
                        logger::log(logger::level::error, std::source_location::current(),
                                    "Server: Too many consecutive errors ({}), breaking loop", msg_errors);
                        break;
                    }
                    continue;
                }

                msg_errors = 0;
                const auto bytes_received = *recv_result;
                metrics_.bytes_received.fetch_add(bytes_received, std::memory_order_relaxed);
                metrics_.messages_handled.fetch_add(1u, std::memory_order_relaxed);

                logger::log(logger::level::debug, std::source_location::current(), "Server: Received {} bytes", bytes_received);

                // Echo the message back to client
                const std::string response =
                    std::format("ECHO: {}", std::string_view(reinterpret_cast<const char*>(buffer.data()), bytes_received));

                logger::log(logger::level::debug, std::source_location::current(), "Server: Sending {} bytes reply...", response.size());

                const std::span<const std::byte> response_span {reinterpret_cast<const std::byte*>(response.data()), response.size()};

                const auto write_result = co_await ep.send(response_span, reinterpret_cast<const sockaddr*>(&peer_addr), peer_addr_len);

                if (!write_result)
                {
                    logger::log(logger::level::warn, std::source_location::current(), "Server: Send error to peer: {}",
                                write_result.error().message());
                    metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
                }
                else
                {
                    metrics_.bytes_sent.fetch_add(*write_result, std::memory_order_relaxed);
                    logger::log(logger::level::debug, std::source_location::current(), "Server: Replied with {} bytes", *write_result);
                }
            }
        }
        catch (const std::exception& e)
        {
            logger::log(logger::level::error, std::source_location::current(), "Server: Exception in server loop: {}", e.what());
        }
        catch (...)
        {
            logger::log(logger::level::error, std::source_location::current(), "Server: Unknown exception in server loop");
        }

        logger::log(logger::level::info, std::source_location::current(), "Server: Shutting down main loop");

        co_return;
    }

    void manager::print_statistics() const
    {
        const auto bytes_recv = metrics_.bytes_received.load(std::memory_order_relaxed);
        const auto bytes_sent = metrics_.bytes_sent.load(std::memory_order_relaxed);
        const auto msgs = metrics_.messages_handled.load(std::memory_order_relaxed);
        const auto errors = metrics_.errors.load(std::memory_order_relaxed);

        std::println("\n╔════════════════════════════════════════╗");
        std::println("║      Server Statistics (Shutdown)      ║");
        std::println("╠════════════════════════════════════════╣");
        std::println("║ Messages Handled: {:>20} ║", msgs);
        std::println("║ Bytes Received: {:>22} ║", bytes_recv);
        std::println("║ Bytes Sent: {:>26} ║", bytes_sent);
        std::println("║ Errors: {:>30} ║", errors);
        std::println("╚════════════════════════════════════════╝");

        logger::log(logger::level::info, std::source_location::current(),
                    "Server: Final stats - {} msgs, {} bytes received, {} bytes sent, {} errors", msgs, bytes_recv, bytes_sent, errors);
    }

    void manager::signal_handler(int signum) noexcept
    {
        if ((signum == SIGINT) || (signum == SIGTERM))
        {
            // Use write to stderr instead of logger to ensure output (logger might be buffered)
            ::write(STDERR_FILENO, "[SIGNAL] Shutdown signal received\n", 35);

            auto* executor = g_executor_ptr.load(std::memory_order_acquire);
            if (executor)
            {
                ::write(STDERR_FILENO, "[SIGNAL] Stopping executor\n", 27);
                executor->stop();
                ::write(STDERR_FILENO, "[SIGNAL] Stop request sent to executor\n", 38);
            }
        }
    }

} // namespace kmx::aio::sample::udp::minimal::server
