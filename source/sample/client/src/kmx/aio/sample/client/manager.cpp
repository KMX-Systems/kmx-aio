#include "kmx/aio/sample/client/manager.hpp"
#include "kmx/aio/sample/common.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <vector>

namespace kmx::aio::sample::client
{
    static constexpr auto mem_order = std::memory_order_relaxed;
    static constexpr std::size_t transfer_limit_bytes = 200u * 1024u;

    bool manager::run() noexcept(false)
    {
        const auto start_time = std::chrono::high_resolution_clock::now();

        logger::log(logger::level::info, std::source_location::current(), "Starting async stress test: {} concurrent connections to {}:{}",
                    config_.num_workers, config_.server_addr, config_.server_port);

        // Prevent SIGPIPE from terminating the process on broken pipe writes.
        std::signal(SIGPIPE, SIG_IGN);

        // Create executor with thread pool
        const executor_config exec_config {
            .thread_count = config_.scheduler_threads, .max_events = config_.max_events, .timeout_ms = config_.timeout_ms};

        executor_ = std::make_shared<executor>(exec_config);

        // Spawn all worker coroutines into the executor
        for (std::uint32_t i {}; i < config_.num_workers; ++i)
        {
            auto stats = create_connection_stats(i);
            executor_->spawn(worker(i, std::move(stats)));
        }

        ui_thread_ = std::jthread([this](std::stop_token stop_token) { ui_loop(stop_token); });

        // Run executor (blocks until all tasks complete)
        executor_->run();

        if (ui_thread_.joinable())
        {
            ui_thread_.request_stop();
            ui_thread_.join();
        }

        const auto end_time = std::chrono::high_resolution_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        print_summary(elapsed);

        const auto failures = metrics_.failures.load(mem_order);
        logger::log(logger::level::info, std::source_location::current(),
                    "Stress test completed in {} ms with {} successes and {} failures", elapsed.count(),
                    metrics_.successes.load(mem_order), failures);

        return failures == 0u;
    }

    std::expected<descriptor::file, std::error_code> manager::create_nonblocking_socket() noexcept
    {
        auto fd_result = descriptor::file::create_socket(AF_INET, SOCK_STREAM, 0);
        if (!fd_result)
            return std::unexpected(fd_result.error());

        auto fd = std::move(*fd_result);

        // Get current flags
        auto flags_result = fd.fcntl(F_GETFL);
        if (!flags_result)
            return std::unexpected(flags_result.error());

        // Set non-blocking flag
        auto setfl_result = fd.fcntl(F_SETFL, *flags_result | O_NONBLOCK);
        if (!setfl_result)
            return std::unexpected(setfl_result.error());

        return fd;
    }

    task<std::expected<tcp::stream, std::error_code>> manager::async_connect() noexcept
    {
        auto fd_result = create_nonblocking_socket();
        if (!fd_result)
            co_return std::unexpected(fd_result.error());

        auto fd_owner = std::move(*fd_result);
        const auto fd = fd_owner.get();

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_port = ::htons(config_.server_port);

        if (auto pton_result = aio::inet_pton(AF_INET, config_.server_addr.data(), &addr.sin_addr); !pton_result)
            co_return std::unexpected(pton_result.error());

        // Attempt non-blocking connect using wrapper
        auto connect_result = fd_owner.connect(reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
        const bool in_progress = (!connect_result && connect_result.error().value() == EINPROGRESS);

        if (!connect_result && !in_progress)
            co_return std::unexpected(connect_result.error());

        // Register with executor for I/O events BEFORE waiting
        if (const auto reg_result = executor_->register_fd(fd); !reg_result)
            co_return std::unexpected(reg_result.error());

        // If connect is in progress, wait for socket to be writable
        if (in_progress)
            co_await executor_->wait_io(fd, event_type::write);

        // Check connection status using wrapper
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (auto sockopt_result = fd_owner.getsockopt(SOL_SOCKET, SO_ERROR, &so_error, &len); !sockopt_result)
        {
            executor_->unregister_fd(fd);
            co_return std::unexpected(sockopt_result.error());
        }

        if (so_error != 0)
        {
            executor_->unregister_fd(fd);
            co_return std::unexpected(std::error_code(so_error, std::generic_category()));
        }

        // On success, transfer ownership of the fd to the stream
        co_return tcp::stream(*executor_, std::move(fd_owner));
    }

    task<void> manager::worker(const std::uint32_t worker_id, std::shared_ptr<connection_stats> stats) noexcept(false)
    {
        metrics_.total_connections.fetch_add(1u, mem_order);

        try
        {
            // Asynchronously connect to server
            auto stream_result = co_await async_connect();
            if (!stream_result)
            {
                logger::log(logger::level::warn, std::source_location::current(), "Worker [{}]: Connection failed: {}", worker_id,
                            stream_result.error().message());
                metrics_.failures.fetch_add(1u, mem_order);
                metrics_.completed.fetch_add(1u, mem_order);
                metrics_.errors.fetch_add(1u, mem_order);
                if (stats)
                {
                    stats->errors.fetch_add(1u, mem_order);
                    stats->closed.store(true, mem_order);
                }
                co_return;
            }

            auto stream_ptr = std::make_shared<tcp::stream>(std::move(*stream_result));
            if (stats)
            {
                stats->rx_active.store(true, mem_order);
            }

            logger::log(logger::level::debug, std::source_location::current(), "Worker [{}]: Connected", worker_id);

            try {
                executor_->spawn(worker_sender(stream_ptr, worker_id, stats));
            } catch (const std::exception& e) {
                logger::log(logger::level::error, std::source_location::current(), "Worker [{}]: Spawn failed: {}", worker_id, e.what());
                throw;
            }

            std::vector<char> buffer(4096);
            std::size_t received_bytes = 0;
            while (true)
            {
                auto recv_result = co_await stream_ptr->read(buffer);
                if (!recv_result)
                {
                    metrics_.errors.fetch_add(1u, mem_order);
                    if (stats)
                    {
                        stats->errors.fetch_add(1u, mem_order);
                    }
                    break;
                }

                if (*recv_result == 0)
                {
                    break;
                }

                received_bytes += *recv_result;
                metrics_.bytes_received.fetch_add(*recv_result, mem_order);
                if (stats)
                {
                    stats->bytes_received.fetch_add(*recv_result, mem_order);
                }
                if (received_bytes >= transfer_limit_bytes)
                {
                    break;
                }

                if (worker_id % 100 == 0) {
                     logger::log(logger::level::info, std::source_location::current(), "Worker [{}]: Received {} bytes", worker_id, *recv_result);
                }

                metrics_.successes.fetch_add(1u, mem_order);
            }

            logger::log(logger::level::info, std::source_location::current(),
                        "Worker [{}]: Received {} bytes", worker_id, received_bytes);
        }
        catch (const std::exception& e)
        {
            logger::log(logger::level::error, std::source_location::current(), "Worker [{}]: Exception: {}", worker_id, e.what());
            metrics_.failures.fetch_add(1u, mem_order);
            metrics_.errors.fetch_add(1u, mem_order);
            if (stats)
            {
                stats->errors.fetch_add(1u, mem_order);
            }
        }

        if (stats)
        {
            stats->rx_active.store(false, mem_order);
            update_closed_state(stats);
        }

        metrics_.completed.fetch_add(1u, mem_order);
        // FD is now automatically unregistered by tcp::stream's destructor
        co_return;
    }

    kmx::aio::task<void> manager::worker_sender(std::shared_ptr<tcp::stream> stream, const std::uint32_t worker_id,
                                                std::shared_ptr<connection_stats> stats) noexcept(false)
    {
        try
        {
            std::vector<char> buffer;
            buffer.reserve(512);
            std::size_t sent_bytes = 0;
            if (stats)
            {
                stats->tx_active.store(true, mem_order);
            }
            while (true)
            {
                if (sent_bytes >= transfer_limit_bytes)
                {
                    break;
                }

                common::generate_random_buffer(buffer);

                const auto remaining = transfer_limit_bytes - sent_bytes;
                if (buffer.size() > remaining)
                {
                    buffer.resize(remaining);
                }

                const std::span<const char> buffer_span {buffer.data(), buffer.size()};
                if (auto res = co_await stream->write_all(buffer_span); !res)
                {
                    metrics_.errors.fetch_add(1u, mem_order);
                    if (stats)
                    {
                        stats->errors.fetch_add(1u, mem_order);
                    }
                    break;
                }

                sent_bytes += buffer.size();
                metrics_.bytes_sent.fetch_add(buffer.size(), mem_order);
                if (stats)
                {
                    stats->bytes_sent.fetch_add(buffer.size(), mem_order);
                }
            }

            ::shutdown(stream->get_fd(), SHUT_WR);

            logger::log(logger::level::info, std::source_location::current(),
                        "Worker [{}]: Sent {} bytes", worker_id, sent_bytes);
        }
        catch (...)
        {
        }
        if (stats)
        {
            stats->tx_active.store(false, mem_order);
            update_closed_state(stats);
        }
        co_return;
    }

    std::shared_ptr<manager::connection_stats> manager::create_connection_stats(const std::uint32_t worker_id)
    {
        auto stats = std::make_shared<connection_stats>();
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_[worker_id] = stats;
        return stats;
    }

    void manager::update_closed_state(const std::shared_ptr<connection_stats>& stats)
    {
        if (!stats)
        {
            return;
        }

        const auto rx_active = stats->rx_active.load(mem_order);
        const auto tx_active = stats->tx_active.load(mem_order);
        if (!rx_active && !tx_active)
        {
            stats->closed.store(true, mem_order);
        }
    }

    void manager::ui_loop(std::stop_token stop_token) const
    {
        using namespace std::chrono_literals;

        while (!stop_token.stop_requested())
        {
            struct snapshot_entry
            {
                std::uint32_t worker_id {};
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
                for (const auto& [worker_id, stats] : connections_)
                {
                    if (!stats)
                    {
                        continue;
                    }
                    snapshot.push_back(snapshot_entry {
                        .worker_id = worker_id,
                        .tx = stats->bytes_sent.load(mem_order),
                        .rx = stats->bytes_received.load(mem_order),
                        .errors = stats->errors.load(mem_order),
                        .tx_active = stats->tx_active.load(mem_order),
                        .rx_active = stats->rx_active.load(mem_order),
                        .closed = stats->closed.load(mem_order),
                    });
                }
            }

            std::sort(snapshot.begin(), snapshot.end(),
                      [](const auto& a, const auto& b) { return a.worker_id < b.worker_id; });

            const auto total = metrics_.total_connections.load(mem_order);
            const auto completed = metrics_.completed.load(mem_order);
            const auto successes = metrics_.successes.load(mem_order);
            const auto failures = metrics_.failures.load(mem_order);
            const auto bytes_sent = metrics_.bytes_sent.load(mem_order);
            const auto bytes_recv = metrics_.bytes_received.load(mem_order);
            const auto errors = metrics_.errors.load(mem_order);

            std::cout << "\x1b[2J\x1b[H";
            std::cout << "Client Live Connection Stats\n";
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
                                             entry.worker_id,
                                             common::format_bytes(entry.tx),
                                             common::format_bytes(entry.rx),
                                             entry.errors,
                                             state);
                }
            }

            std::cout << "────────────────────────────────────────────────────────────────────────\n";
            std::cout << std::format("Client Totals: TX {} | RX {} | EC {} | Completed {} | Total {} | OK {} | Fail {}\n",
                                     common::format_bytes(bytes_sent),
                                     common::format_bytes(bytes_recv),
                                     errors,
                                     completed,
                                     total,
                                     successes,
                                     failures);
            std::cout << std::flush;

            std::this_thread::sleep_for(250ms);
        }
    }

    void manager::print_summary(const std::chrono::milliseconds elapsed) const
    {
        const auto total = metrics_.total_connections.load(mem_order);
        const auto successes = metrics_.successes.load(mem_order);
        const auto failures = metrics_.failures.load(mem_order);
        const auto success_rate = (total > 0) ? ((successes * 100) / total) : 0;

        std::cout << "\n";
        std::cout << "╔════════════════════════════════════════╗\n";
        std::cout << "║       Client Test Results Summary      ║\n";
        std::cout << "╠════════════════════════════════════════╣\n";
        std::cout << std::format("║ Total Connections: {:>19} ║\n", total);
        std::cout << std::format("║ Successes: {:>27} ║\n", successes);
        std::cout << std::format("║ Failures: {:>28} ║\n", failures);
        std::cout << std::format("║ Success Rate: {:>23}% ║\n", success_rate);
        std::cout << std::format("║ Elapsed Time: {:>21} ms ║\n", elapsed.count());
        std::cout << "╚════════════════════════════════════════╝\n";
    }

} // namespace kmx::aio::sample::client
