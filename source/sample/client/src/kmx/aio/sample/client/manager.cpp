#include "kmx/aio/sample/client/manager.hpp"

namespace kmx::aio::sample::client
{
    static constexpr auto mem_order = std::memory_order_relaxed;

    int manager::run() noexcept(false)
    {
        const auto start_time = std::chrono::high_resolution_clock::now();

        logger::log(logger::level::info, std::source_location::current(), "Starting async stress test: {} concurrent connections to {}:{}",
                    config_.num_workers, config_.server_addr, config_.server_port);

        // Create executor with thread pool
        const executor_config exec_config {
            .thread_count = config_.scheduler_threads, .max_events = config_.max_events, .timeout_ms = config_.timeout_ms};

        executor_ = std::make_shared<executor>(exec_config);

        // Spawn all worker coroutines into the executor
        for (std::size_t i {}; i < static_cast<std::size_t>(config_.num_workers); ++i)
            executor_->spawn(worker(static_cast<int>(i)));

        // Run executor (blocks until all tasks complete)
        executor_->run();

        const auto end_time = std::chrono::high_resolution_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        print_summary(elapsed);

        const auto failures = metrics_.failures.load(mem_order);
        logger::log(logger::level::info, std::source_location::current(),
                    "Stress test completed in {} ms with {} successes and {} failures", elapsed.count(),
                    metrics_.successes.load(mem_order), failures);

        return failures > 0 ? 1 : 0;
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

    task<void> manager::worker(const int worker_id) noexcept(false)
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
                co_return;
            }

            auto stream = std::move(*stream_result);

            logger::log(logger::level::debug, std::source_location::current(), "Worker [{}]: Connected", worker_id);

            // Asynchronously send message using write_all
            std::vector<char> message_buffer(config_.message.begin(), config_.message.end());
            if (auto send_result = co_await stream.write_all(std::move(message_buffer)); !send_result)
            {
                logger::log(logger::level::warn, std::source_location::current(), "Worker [{}]: Send failed: {}", worker_id,
                            send_result.error().message());
                metrics_.failures.fetch_add(1u, mem_order);
                metrics_.completed.fetch_add(1u, mem_order);
                co_return;
            }

            logger::log(logger::level::debug, std::source_location::current(), "Worker [{}]: Message sent", worker_id);

            // Asynchronously receive response
            std::vector<char> response_buffer(1024);
            auto recv_result = co_await stream.read({response_buffer.data(), response_buffer.size()});
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
        // FD is now automatically unregistered by tcp::stream's destructor
        co_return;
    }

    void manager::print_summary(const std::chrono::milliseconds elapsed) const
    {
        const auto total = metrics_.total_connections.load(mem_order);
        const auto successes = metrics_.successes.load(mem_order);
        const auto failures = metrics_.failures.load(mem_order);
        const auto success_rate = (total > 0) ? ((successes * 100) / total) : 0;

        std::println("\n");
        std::println("╔════════════════════════════════════════╗");
        std::println("║       Client Test Results Summary      ║");
        std::println("╠════════════════════════════════════════╣");
        std::println("║ Total Connections: {:>19} ║", total);
        std::println("║ Successes: {:>27} ║", successes);
        std::println("║ Failures: {:>28} ║", failures);
        std::println("║ Success Rate: {:>23}% ║", success_rate);
        std::println("║ Elapsed Time: {:>21} ms ║", elapsed.count());
        std::println("╚════════════════════════════════════════╝");
    }

} // namespace kmx::aio::sample::client
