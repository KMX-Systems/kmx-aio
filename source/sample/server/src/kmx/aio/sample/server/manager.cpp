#include "kmx/aio/sample/server/manager.hpp"
#include "kmx/aio/sample/common.hpp"

#include <algorithm>
#include <chrono>
#include <span>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <vector>

namespace kmx::aio::sample::server
{
    static constexpr std::size_t transfer_limit_bytes = 200u * 1024u;
    bool manager::run() noexcept(false)
    {
        logger::log(logger::level::info, std::source_location::current(),
                    "═══════════════════════════════════════════════════════════════");
        logger::log(logger::level::info, std::source_location::current(), "Echo Server Starting");
        logger::log(logger::level::info, std::source_location::current(),
                    "═══════════════════════════════════════════════════════════════");

        // Prevent SIGPIPE from terminating the process on broken pipe writes.
        std::signal(SIGPIPE, SIG_IGN);

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

        ui_thread_ = std::jthread([this](std::stop_token stop_token) { ui_loop(stop_token); });

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

        if (ui_thread_.joinable())
        {
            ui_thread_.request_stop();
            ui_thread_.join();
        }

        print_metrics();

        logger::log(logger::level::info, std::source_location::current(),
                    "═══════════════════════════════════════════════════════════════");
        logger::log(logger::level::info, std::source_location::current(), "Server: Shutdown complete");
        logger::log(logger::level::info, std::source_location::current(),
                    "═══════════════════════════════════════════════════════════════");

        return metrics_.errors == 0u;
    }

    kmx::aio::task<void> manager::handle_client(kmx::aio::tcp::stream stream, const std::uint64_t client_id,
                                                std::shared_ptr<connection_stats> stats) noexcept(false)
    {
        logger::log(logger::level::info, std::source_location::current(), "Client [{}]: Handler starting", client_id);

        metrics_.active_connections.fetch_add(1u, std::memory_order_relaxed);

        logger::log(logger::level::info, std::source_location::current(), "Client [{}]: Connected | Active connections: {}", client_id,
                    metrics_.active_connections.load());

        auto stream_ptr = std::make_shared<kmx::aio::tcp::stream>(std::move(stream));
        stats->rx_active.store(true, std::memory_order_relaxed);

        try
        {
            executor_->spawn(client_sender(stream_ptr, client_id, stats));

            std::vector<char> buffer(4096u);
            std::size_t messages_received = 0;
            std::size_t received_bytes = 0;

            while (true)
            {
                // Read from client
                // logger::log(logger::level::debug, std::source_location::current(), "Client [{}]: Waiting for data...", client_id);

                const auto read_result = co_await stream_ptr->read(buffer);
                if (!read_result)
                {
                    if (read_result.error().value() != 0)
                    {
                        logger::log(logger::level::warn, std::source_location::current(), "Client [{}]: Read error: {}", client_id,
                                    read_result.error().message());
                        metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
                        stats->errors.fetch_add(1u, std::memory_order_relaxed);
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

                received_bytes += bytes_read;
                if (received_bytes >= transfer_limit_bytes)
                {
                    break;
                }

                ++messages_received;
                metrics_.bytes_received.fetch_add(bytes_read, std::memory_order_relaxed);
                stats->bytes_received.fetch_add(bytes_read, std::memory_order_relaxed);

                // logger::log(logger::level::debug, std::source_location::current(),
                //            "Client [{}]: Received {} bytes (message #{}) | Total bytes: {}", client_id, bytes_read, messages_received,
                //            metrics_.bytes_received.load());
            }

            logger::log(logger::level::info, std::source_location::current(), "Client [{}]: Connection closed | Messages: {} recv",
                        client_id, messages_received);
            logger::log(logger::level::info, std::source_location::current(), "Client [{}]: Received {} bytes", client_id,
                        received_bytes);
        }
        catch (const std::exception& e)
        {
            logger::log(logger::level::error, std::source_location::current(), "Client [{}]: Exception: {}", client_id, e.what());
            metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
            stats->errors.fetch_add(1u, std::memory_order_relaxed);
        }

        stats->rx_active.store(false, std::memory_order_relaxed);
        update_closed_state(stats);

        metrics_.active_connections.fetch_sub(1, std::memory_order_relaxed);
        logger::log(logger::level::info, std::source_location::current(), "Client [{}]: Disconnected | Active connections: {}", client_id,
                    metrics_.active_connections.load());

        co_return;
    }

    kmx::aio::task<void> manager::client_sender(std::shared_ptr<tcp::stream> stream, const std::uint64_t client_id,
                                                std::shared_ptr<connection_stats> stats) noexcept(false)
    {
        try
        {
            std::vector<char> buffer;
            buffer.reserve(512);
            std::size_t sent_bytes = 0;
            stats->tx_active.store(true, std::memory_order_relaxed);
            while (true)
            {
                if (sent_bytes >= transfer_limit_bytes)
                {
                    break;
                }

                common::generate_random_buffer(buffer);
                const auto size = buffer.size();
                const auto remaining = transfer_limit_bytes - sent_bytes;
                if (size > remaining)
                {
                    buffer.resize(remaining);
                }
                const std::span<const char> buffer_span {buffer.data(), buffer.size()};
                if (auto res = co_await stream->write_all(buffer_span); !res)
                {
                    metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
                    stats->errors.fetch_add(1u, std::memory_order_relaxed);
                    break;
                }
                sent_bytes += buffer.size();
                metrics_.bytes_sent.fetch_add(buffer.size(), std::memory_order_relaxed);
                stats->bytes_sent.fetch_add(buffer.size(), std::memory_order_relaxed);
            }

            ::shutdown(stream->get_fd(), SHUT_WR);

            logger::log(logger::level::info, std::source_location::current(),
                        "Client [{}]: Sent {} bytes", client_id, sent_bytes);
        }
        catch (...)
        {
        }
        stats->tx_active.store(false, std::memory_order_relaxed);
        update_closed_state(stats);
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

                auto stats = create_connection_stats(client_id);

                logger::log(logger::level::info, std::source_location::current(),
                            "Acceptor: Accepted connection #{} (total: {}, active: {})", client_id, metrics_.total_connections.load(),
                            metrics_.active_connections.load());

                // Create stream from accepted file descriptor
                kmx::aio::descriptor::file client_fd = std::move(*accept_result);
                auto client_stream = kmx::aio::tcp::stream(*executor_, std::move(client_fd));

                logger::log(logger::level::debug, std::source_location::current(), "Acceptor: Stream created for client [{}]", client_id);

                // Spawn handler for this client as a new task
                logger::log(logger::level::debug, std::source_location::current(), "Acceptor: Spawning handler for client [{}]", client_id);

                auto handler_task = handle_client(std::move(client_stream), client_id, std::move(stats));

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

    std::shared_ptr<manager::connection_stats> manager::create_connection_stats(const std::uint64_t client_id)
    {
        auto stats = std::make_shared<connection_stats>();
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_[client_id] = stats;
        return stats;
    }

    void manager::update_closed_state(const std::shared_ptr<connection_stats>& stats)
    {
        if (!stats)
        {
            return;
        }

        const auto rx_active = stats->rx_active.load(std::memory_order_relaxed);
        const auto tx_active = stats->tx_active.load(std::memory_order_relaxed);
        if (!rx_active && !tx_active)
        {
            stats->closed.store(true, std::memory_order_relaxed);
        }
    }

    void manager::ui_loop(std::stop_token stop_token) const
    {
        using namespace std::chrono_literals;

        while (!stop_token.stop_requested())
        {
            struct snapshot_entry
            {
                std::uint64_t client_id {};
                std::uint64_t tx {};
                std::uint64_t rx {};
                std::uint64_t errors {};
                bool tx_active {};
                bool rx_active {};
                bool closed {};
            };

            std::vector<snapshot_entry> snapshot;
            {
                std::lock_guard<std::mutex> lock(connections_mutex_);
                snapshot.reserve(connections_.size());
                for (const auto& [client_id, stats] : connections_)
                {
                    if (!stats)
                    {
                        continue;
                    }
                    snapshot.push_back(snapshot_entry {
                        .client_id = client_id,
                        .tx = stats->bytes_sent.load(std::memory_order_relaxed),
                        .rx = stats->bytes_received.load(std::memory_order_relaxed),
                        .errors = stats->errors.load(std::memory_order_relaxed),
                        .tx_active = stats->tx_active.load(std::memory_order_relaxed),
                        .rx_active = stats->rx_active.load(std::memory_order_relaxed),
                        .closed = stats->closed.load(std::memory_order_relaxed),
                    });
                }
            }

            std::sort(snapshot.begin(), snapshot.end(), [](const auto& a, const auto& b) { return a.client_id < b.client_id; });

            const auto total_conn = metrics_.total_connections.load(std::memory_order_relaxed);
            const auto active_conn = metrics_.active_connections.load(std::memory_order_relaxed);
            const auto bytes_recv = metrics_.bytes_received.load(std::memory_order_relaxed);
            const auto bytes_sent = metrics_.bytes_sent.load(std::memory_order_relaxed);
            const auto errors = metrics_.errors.load(std::memory_order_relaxed);

            std::cout << "\x1b[2J\x1b[H";
            std::cout << "Live Connection Stats\n";
            std::cout << "────────────────────────────────────────────────────────────────────────\n";

            if (snapshot.empty())
            {
                std::cout << "(no active connections)\n";
            }
            else
            {
                for (const auto& entry : snapshot)
                {
                    std::string_view state = "-";
                    if (entry.closed)
                    {
                        state = "C";
                    }
                    else if (entry.tx_active && entry.rx_active)
                    {
                        state = "TX+RX";
                    }
                    else if (entry.tx_active)
                    {
                        state = "TX";
                    }
                    else if (entry.rx_active)
                    {
                        state = "RX";
                    }

                    std::cout << std::format("Connection {:07}: TX {:>10} | RX {:>10} | EC {:05} | {}\n",
                                             entry.client_id,
                                             common::format_bytes(entry.tx),
                                             common::format_bytes(entry.rx),
                                             entry.errors,
                                             state);
                }
            }

            std::cout << "────────────────────────────────────────────────────────────────────────\n";
            std::cout << std::format("Server Totals: TX {} | RX {} | EC {} | Active {} | Total {}\n",
                                     common::format_bytes(bytes_sent),
                                     common::format_bytes(bytes_recv),
                                     errors,
                                     active_conn,
                                     total_conn);
            std::cout << std::flush;

            std::this_thread::sleep_for(250ms);
        }
    }

    void manager::print_metrics() const
    {
        const auto total_conn = metrics_.total_connections.load(std::memory_order_relaxed);
        const auto active_conn = metrics_.active_connections.load(std::memory_order_relaxed);
        const auto bytes_recv = metrics_.bytes_received.load(std::memory_order_relaxed);
        const auto bytes_sent = metrics_.bytes_sent.load(std::memory_order_relaxed);
        const auto errors = metrics_.errors.load(std::memory_order_relaxed);

        std::cout << "\n╔════════════════════════════════════════╗\n";
        std::cout << "║        Server Metrics (Shutdown)       ║\n";
        std::cout << "╠════════════════════════════════════════╣\n";
        std::cout << std::format("║ Total Connections: {:>19} ║\n", total_conn);
        std::cout << std::format("║ Active Connections: {:>18} ║\n", active_conn);
        std::cout << std::format("║ Bytes Received: {:>22} ║\n", bytes_recv);
        std::cout << std::format("║ Bytes Sent: {:>26} ║\n", bytes_sent);
        std::cout << std::format("║ Errors: {:>30} ║\n", errors);
        std::cout << "╚════════════════════════════════════════╝\n";

        logger::log(logger::level::info, std::source_location::current(),
                "Server: Final metrics - {} total connections, {} bytes received, {} bytes sent, {} errors", total_conn, bytes_recv,
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
