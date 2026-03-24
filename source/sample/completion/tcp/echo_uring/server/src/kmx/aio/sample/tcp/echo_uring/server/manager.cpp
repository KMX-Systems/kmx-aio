#include "kmx/aio/sample/tcp/echo_uring/server/manager.hpp"
#include "kmx/aio/sample/tcp/echo/common.hpp"

#include <algorithm>
#include <span>
#include <sys/socket.h>
#include <thread>
#include <vector>

namespace kmx::aio::sample::tcp::echo_uring::server
{
    static constexpr std::size_t transfer_limit_bytes = 200u * 1024u;
    static constexpr std::size_t max_buffers = 2048u;
    static constexpr std::size_t buffer_size = 8192u;

    int manager::allocate_buffer()
    {
        std::lock_guard<std::mutex> lock(buf_mutex_);
        if (free_buf_indices_.empty())
            return -1;
        int idx = free_buf_indices_.back();
        free_buf_indices_.pop_back();
        return idx;
    }

    void manager::free_buffer(int index)
    {
        if (index < 0)
            return;
        std::lock_guard<std::mutex> lock(buf_mutex_);
        free_buf_indices_.push_back(index);
    }

    bool manager::run() noexcept(false)
    {
        logger::log(logger::level::info, std::source_location::current(),
                    "═══════════════════════════════════════════════════════════════");
        logger::log(logger::level::info, std::source_location::current(), "Echo Uring Server Starting");
        logger::log(logger::level::info, std::source_location::current(),
                    "═══════════════════════════════════════════════════════════════");

        std::signal(SIGPIPE, SIG_IGN);

        const kmx::aio::completion::executor_config exec_config {.ring_entries = config_.epoll_max_events,
                                                                 .max_completions = config_.epoll_max_events,
                                                                 .thread_count = config_.scheduler_threads,
                                                                 .core_id = -1};

        executor_ = std::make_shared<kmx::aio::completion::executor>(exec_config);
        g_executor_ptr.store(executor_.get(), std::memory_order_release);

        // Pre-allocate and register buffers
        buffer_memory_.resize(max_buffers * buffer_size);
        registered_buffers_.resize(max_buffers);
        free_buf_indices_.reserve(max_buffers);

        for (std::size_t i = 0; i < max_buffers; ++i)
        {
            registered_buffers_[i].iov_base = buffer_memory_.data() + (i * buffer_size);
            registered_buffers_[i].iov_len = buffer_size;
            free_buf_indices_.push_back(static_cast<int>(i)); // LIFO
        }

        auto reg_res = executor_->register_buffers(std::span<const ::iovec> {registered_buffers_.data(), registered_buffers_.size()});
        if (!reg_res)
        {
            logger::log(logger::level::error, std::source_location::current(), "Failed to register buffers: {}", reg_res.error().message());
            return false;
        }

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        auto acceptor_task = connection_acceptor();
        executor_->spawn(std::move(acceptor_task));

        const auto bind_ip = kmx::aio::ip_to_string(config_.bind_address);
        ui_thread_ = std::jthread([this](std::stop_token stop_token) { ui_loop(stop_token); });

        executor_->run();

        if (ui_thread_.joinable())
        {
            ui_thread_.request_stop();
            ui_thread_.join();
        }

        (void) executor_->unregister_buffers();
        print_metrics();
        return metrics_.errors == 0u;
    }

    kmx::aio::task<void> manager::handle_client(kmx::aio::completion::tcp::stream stream, const std::uint64_t client_id,
                                                std::shared_ptr<connection_stats> stats) noexcept(false)
    {
        metrics_.active_connections.fetch_add(1u, std::memory_order_relaxed);
        auto stream_ptr = std::make_shared<kmx::aio::completion::tcp::stream>(std::move(stream));
        stats->rx_active.store(true, std::memory_order_relaxed);

        int rx_buf_idx = allocate_buffer();
        if (rx_buf_idx < 0)
        {
            metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
            co_return; // out of buffers
        }
        std::span<char> buffer_span(static_cast<char*>(registered_buffers_[rx_buf_idx].iov_base), buffer_size);

        try
        {
            executor_->spawn(client_sender(stream_ptr, client_id, stats));

            std::size_t messages_received = 0;
            std::size_t received_bytes = 0;

            while (true)
            {
                const auto read_result = co_await stream_ptr->read_fixed(buffer_span, rx_buf_idx);
                if (!read_result)
                {
                    if (read_result.error().value() != 0)
                    {
                        metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
                        stats->errors.fetch_add(1u, std::memory_order_relaxed);
                    }
                    break;
                }

                const auto bytes_read = *read_result;
                if (bytes_read == 0)
                    break;

                received_bytes += bytes_read;
                if (received_bytes >= transfer_limit_bytes)
                    break;

                ++messages_received;
                metrics_.bytes_received.fetch_add(bytes_read, std::memory_order_relaxed);
                stats->bytes_received.fetch_add(bytes_read, std::memory_order_relaxed);
            }
        }
        catch (const std::exception& e)
        {
            metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
            stats->errors.fetch_add(1u, std::memory_order_relaxed);
        }

        free_buffer(rx_buf_idx);

        stats->rx_active.store(false, std::memory_order_relaxed);
        update_closed_state(stats);
        metrics_.active_connections.fetch_sub(1, std::memory_order_relaxed);
        co_return;
    }

    kmx::aio::task<void> manager::client_sender(std::shared_ptr<kmx::aio::completion::tcp::stream> stream,
                                                const std::uint64_t /* client_id */,
                                                std::shared_ptr<connection_stats> stats) noexcept(false)
    {
        int tx_buf_idx = allocate_buffer();
        if (tx_buf_idx < 0)
        {
            co_return;
        }

        try
        {
            std::span<char> buffer_span(static_cast<char*>(registered_buffers_[tx_buf_idx].iov_base), buffer_size);
            std::size_t sent_bytes = 0;
            stats->tx_active.store(true, std::memory_order_relaxed);

            while (true)
            {
                if (sent_bytes >= transfer_limit_bytes)
                    break;

                const auto remaining = transfer_limit_bytes - sent_bytes;
                const auto chunk_size = std::min(buffer_size, remaining);

                // Usually we'd fill the buffer. For simple echo imitation, we just use random bytes.
                for (std::size_t i = 0; i < chunk_size; ++i)
                {
                    buffer_span[i] = static_cast<char>(i % 256);
                }

                const std::span<const char> write_span {buffer_span.data(), chunk_size};
                if (auto res = co_await stream->write_all_fixed(write_span, tx_buf_idx); !res)
                {
                    metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
                    stats->errors.fetch_add(1u, std::memory_order_relaxed);
                    break;
                }
                sent_bytes += chunk_size;
                metrics_.bytes_sent.fetch_add(chunk_size, std::memory_order_relaxed);
                stats->bytes_sent.fetch_add(chunk_size, std::memory_order_relaxed);
            }

            ::shutdown(stream->get_fd(), SHUT_WR);
        }
        catch (...)
        {
        }

        free_buffer(tx_buf_idx);

        stats->tx_active.store(false, std::memory_order_relaxed);
        update_closed_state(stats);
        co_return;
    }

    kmx::aio::task<void> manager::connection_acceptor() noexcept(false)
    {
        auto listener = kmx::aio::completion::tcp::listener(executor_, config_.bind_address, config_.bind_port);

        if (const auto listen_result = listener.listen(128); !listen_result)
        {
            co_return;
        }

        std::uint64_t client_counter = 0;
        std::uint64_t accept_errors = 0;

        try
        {
            while (true)
            {
                auto accept_result = co_await listener.accept();
                if (!accept_result)
                {
                    metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
                    ++accept_errors;
                    if (accept_errors > 5)
                        break;
                    continue;
                }

                accept_errors = 0;
                metrics_.total_connections.fetch_add(1u, std::memory_order_relaxed);
                const auto client_id = ++client_counter;
                auto stats = create_connection_stats(client_id);

                auto client_fd = std::move(*accept_result);
                auto client_stream = kmx::aio::completion::tcp::stream(executor_, std::move(client_fd));

                auto handler_task = handle_client(std::move(client_stream), client_id, std::move(stats));
                executor_->spawn(std::move(handler_task));
            }
        }
        catch (...)
        {
        }

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
            return;

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
            const auto total_conn = metrics_.total_connections.load(std::memory_order_relaxed);
            const auto active_conn = metrics_.active_connections.load(std::memory_order_relaxed);
            const auto bytes_recv = metrics_.bytes_received.load(std::memory_order_relaxed);
            const auto bytes_sent = metrics_.bytes_sent.load(std::memory_order_relaxed);
            const auto errors = metrics_.errors.load(std::memory_order_relaxed);

            std::cout << "\x1b[2J\x1b[H";
            std::cout << "Live Uring Connection Stats\n";
            std::cout << "────────────────────────────────────────────────────────────────────────\n";
            std::cout << std::format("Server Totals: TX {} | RX {} | EC {} | Active {} | Total {}\n",
                                     ::kmx::aio::sample::common::format_bytes(bytes_sent),
                                     ::kmx::aio::sample::common::format_bytes(bytes_recv), errors, active_conn, total_conn);
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
        std::cout << "║    Uring Server Metrics (Shutdown)     ║\n";
        std::cout << "╠════════════════════════════════════════╣\n";
        std::cout << std::format("║ Total Connections: {:>19} ║\n", total_conn);
        std::cout << std::format("║ Active Connections: {:>18} ║\n", active_conn);
        std::cout << std::format("║ Bytes Received: {:>22} ║\n", bytes_recv);
        std::cout << std::format("║ Bytes Sent: {:>26} ║\n", bytes_sent);
        std::cout << std::format("║ Errors: {:>30} ║\n", errors);
        std::cout << "╚════════════════════════════════════════╝\n";
    }

    void manager::signal_handler(int signum) noexcept
    {
        if ((signum == SIGINT) || (signum == SIGTERM))
        {
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

} // namespace kmx::aio::sample::tcp::echo_uring::server
