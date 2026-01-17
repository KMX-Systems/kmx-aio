#include "kmx/aio/sample/simple_server/manager.hpp"

namespace kmx::aio::sample::simple_server
{
    bool manager::run() noexcept(false)
    {
        logger::log(logger::level::info, std::source_location::current(),
                    "═══════════════════════════════════════════════════════════════");
        logger::log(logger::level::info, std::source_location::current(), "Echo Server Starting");
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

        // Spawn the connection acceptor as a task
        logger::log(logger::level::info, std::source_location::current(), "Server: Spawning connection acceptor coroutine");

        auto acceptor_task = connection_acceptor();
        executor_->spawn(std::move(acceptor_task));

        logger::log(logger::level::info, std::source_location::current(),
                    "═══════════════════════════════════════════════════════════════");
        logger::log(logger::level::info, std::source_location::current(), "Server: Started. Listening on {}:{}", config_.bind_address,
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

    kmx::aio::task<void> manager::handle_client(kmx::aio::tcp::stream stream, const std::uint64_t client_id) noexcept(false)
    {
        logger::log(logger::level::info, std::source_location::current(), "Client [{}]: Handler starting", client_id);

        metrics_.active_connections.fetch_add(1u, std::memory_order_relaxed);

        logger::log(logger::level::info, std::source_location::current(), "Client [{}]: Connected | Active connections: {}", client_id,
                    metrics_.active_connections.load());

        try
        {
            std::vector<char> buffer(4096u);
            std::size_t messages_received = 0;
            std::size_t messages_sent = 0;

            while (true)
            {
                // Read from client
                logger::log(logger::level::debug, std::source_location::current(), "Client [{}]: Waiting for data...", client_id);

                const auto read_result = co_await stream.read(buffer);
                if (!read_result)
                {
                    if (read_result.error().value() != 0)
                    {
                        logger::log(logger::level::warn, std::source_location::current(), "Client [{}]: Read error: {}", client_id,
                                    read_result.error().message());
                        metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
                    }
                    break;
                }

                const auto bytes_read = *read_result;
                if (bytes_read == 0)
                {
                    // Client disconnected gracefully
                    logger::log(logger::level::debug, std::source_location::current(), "Client [{}]: EOF received", client_id);
                    break;
                }

                ++messages_received;
                metrics_.bytes_received.fetch_add(bytes_read, std::memory_order_relaxed);

                logger::log(logger::level::debug, std::source_location::current(),
                            "Client [{}]: Received {} bytes (message #{}) | Total bytes: {}", client_id, bytes_read, messages_received,
                            metrics_.bytes_received.load());

                // Echo the message back to client
                const std::string response = std::format("ECHO: {}", std::string_view(buffer.data(), bytes_read));

                logger::log(logger::level::debug, std::source_location::current(), "Client [{}]: Sending {} bytes...", client_id,
                            response.size());

                const auto write_result = co_await stream.write(std::span<const char>(response.data(), response.size()));

                if (!write_result)
                {
                    logger::log(logger::level::warn, std::source_location::current(), "Client [{}]: Write error: {}", client_id,
                                write_result.error().message());
                    metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
                    break;
                }

                ++messages_sent;
                metrics_.bytes_sent.fetch_add(*write_result, std::memory_order_relaxed);

                logger::log(logger::level::debug, std::source_location::current(),
                            "Client [{}]: Sent {} bytes (message #{}) | Total bytes: {}", client_id, *write_result, messages_sent,
                            metrics_.bytes_sent.load());
            }

            logger::log(logger::level::info, std::source_location::current(), "Client [{}]: Connection closed | Messages: {} recv, {} sent",
                        client_id, messages_received, messages_sent);
        }
        catch (const std::exception& e)
        {
            logger::log(logger::level::error, std::source_location::current(), "Client [{}]: Exception: {}", client_id, e.what());
            metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
        }

        metrics_.active_connections.fetch_sub(1, std::memory_order_relaxed);
        logger::log(logger::level::info, std::source_location::current(), "Client [{}]: Disconnected | Active connections: {}", client_id,
                    metrics_.active_connections.load());

        co_return;
    }

    kmx::aio::task<void> manager::connection_acceptor() noexcept(false)
    {
        // Create and configure the listener
        logger::log(logger::level::info, std::source_location::current(), "Acceptor: Creating listener on {}:{}", config_.bind_address,
                    config_.bind_port);

        auto listener = kmx::aio::tcp::listener(*executor_, config_.bind_address, config_.bind_port);

        if (const auto listen_result = listener.listen(128); !listen_result)
        {
            logger::log(logger::level::error, std::source_location::current(), "Acceptor: Failed to listen: {}",
                        listen_result.error().message());
            co_return;
        }

        logger::log(logger::level::info, std::source_location::current(), "Acceptor: Listening on {}:{} (backlog: 128)",
                    config_.bind_address, config_.bind_port);

        std::uint64_t client_counter = 0;
        std::uint64_t accept_count = 0;
        std::uint64_t accept_errors = 0;

        try
        {
            while (true)
            {
                logger::log(logger::level::debug, std::source_location::current(), "Acceptor: Waiting for incoming connections...");

                // Accept incoming connection
                auto accept_result = co_await listener.accept();

                logger::log(logger::level::debug, std::source_location::current(), "Acceptor: Accept returned, result valid: {}",
                            static_cast<bool>(accept_result));

                if (!accept_result)
                {
                    logger::log(logger::level::warn, std::source_location::current(), "Acceptor: Accept error: {}",
                                accept_result.error().message());
                    metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
                    ++accept_errors;

                    // Prevent tight loop: small delay before retrying
                    if (accept_errors > 5)
                    {
                        logger::log(logger::level::error, std::source_location::current(),
                                    "Acceptor: Too many consecutive errors ({}), breaking accept loop", accept_errors);
                        break;
                    }

                    continue;
                }

                accept_errors = 0; // Reset error counter on success

                ++accept_count;
                metrics_.total_connections.fetch_add(1u, std::memory_order_relaxed);
                const auto client_id = ++client_counter;

                logger::log(logger::level::info, std::source_location::current(),
                            "Acceptor: Accepted connection #{} (total: {}, active: {})", client_id, metrics_.total_connections.load(),
                            metrics_.active_connections.load());

                // Create stream from accepted file descriptor
                kmx::aio::descriptor::file client_fd = std::move(*accept_result);
                auto client_stream = kmx::aio::tcp::stream(*executor_, std::move(client_fd));

                logger::log(logger::level::debug, std::source_location::current(), "Acceptor: Stream created for client [{}]", client_id);

                // Spawn handler for this client as a new task
                logger::log(logger::level::debug, std::source_location::current(), "Acceptor: Spawning handler for client [{}]", client_id);

                auto handler_task = handle_client(std::move(client_stream), client_id);

                logger::log(logger::level::debug, std::source_location::current(), "Acceptor: Handler task created for client [{}]",
                            client_id);

                executor_->spawn(std::move(handler_task));

                logger::log(logger::level::debug, std::source_location::current(),
                            "Acceptor: Handler spawned for client [{}], returning to accept loop", client_id);
            }
        }
        catch (const std::exception& e)
        {
            logger::log(logger::level::error, std::source_location::current(), "Acceptor: Exception in accept loop: {}", e.what());
        }
        catch (...)
        {
            logger::log(logger::level::error, std::source_location::current(), "Acceptor: Unknown exception in accept loop");
        }

        logger::log(logger::level::info, std::source_location::current(),
                    "Acceptor: Shutting down | Total accepted: {} | Accept errors: {}", accept_count, accept_errors);

        co_return;
    }

    void manager::print_statistics() const
    {
        const auto total_conn = metrics_.total_connections.load(std::memory_order_relaxed);
        const auto active_conn = metrics_.active_connections.load(std::memory_order_relaxed);
        const auto bytes_recv = metrics_.bytes_received.load(std::memory_order_relaxed);
        const auto bytes_sent = metrics_.bytes_sent.load(std::memory_order_relaxed);
        const auto errors = metrics_.errors.load(std::memory_order_relaxed);

        std::println("\n╔════════════════════════════════════════╗");
        std::println("║      Server Statistics (Shutdown)      ║");
        std::println("╠════════════════════════════════════════╣");
        std::println("║ Total Connections: {:>19} ║", total_conn);
        std::println("║ Active Connections: {:>18} ║", active_conn);
        std::println("║ Bytes Received: {:>22} ║", bytes_recv);
        std::println("║ Bytes Sent: {:>26} ║", bytes_sent);
        std::println("║ Errors: {:>30} ║", errors);
        std::println("╚════════════════════════════════════════╝");

        logger::log(logger::level::info, std::source_location::current(),
                    "Server: Final stats - {} total connections, {} bytes received, {} bytes sent, {} errors", total_conn, bytes_recv,
                    bytes_sent, errors);
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

} // namespace kmx::aio::sample::server
