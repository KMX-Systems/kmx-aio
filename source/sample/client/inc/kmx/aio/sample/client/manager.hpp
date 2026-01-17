#include <kmx/aio/descriptor/file.hpp>
#include <kmx/aio/executor.hpp>
#include <kmx/aio/task.hpp>
#include <kmx/aio/tcp/listener.hpp>
#include <kmx/aio/tcp/stream.hpp>
#include <kmx/logger.hpp>

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <expected>
#include <fcntl.h>
#include <format>
#include <iostream>
#include <mutex>
#include <random>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <sys/socket.h>

namespace kmx::aio::sample::client
{
    // Configuration
    struct config
    {
        std::uint32_t num_workers = 1000u;
        std::string_view server_addr = "127.0.0.1";
        std::uint16_t server_port = 8080u;
        std::string_view message = "Is there anybody out there?";
        std::uint32_t scheduler_threads = 4u;
        std::uint16_t max_events = 1024u;
        std::uint16_t timeout_ms = 100u;
    };

    // Metrics
    struct metric_data
    {
        std::atomic_size_t successes {};
        std::atomic_size_t failures {};
        std::atomic_size_t total_connections {};
        std::atomic_size_t completed {};
        std::atomic_uint64_t bytes_sent {};
        std::atomic_uint64_t bytes_received {};
        std::atomic_uint64_t errors {};
    };

    /// @brief Stress test manager class
    class manager
    {
    public:
        explicit manager(config config = {}): config_(std::move(config)) {}

        const metric_data& metrics() const noexcept { return metrics_; }

        /// @brief Run the stress test.
        /// @return Number of failures.
        [[nodiscard]] bool run() noexcept(false);

    private:
        struct connection_stats
        {
            std::atomic_uint64_t bytes_received {};
            std::atomic_uint64_t bytes_sent {};
            std::atomic_uint64_t errors {};
            std::atomic_bool rx_active {false};
            std::atomic_bool tx_active {false};
            std::atomic_bool closed {false};
        };

        /// @brief Creates a non-blocking socket
        [[nodiscard]] static std::expected<descriptor::file, std::error_code> create_nonblocking_socket() noexcept;

        /// @brief Asynchronously connects to the server and returns a TCP stream
        [[nodiscard]] task<std::expected<tcp::stream, std::error_code>> async_connect() noexcept;

        /// @brief Worker coroutine that performs an async stress test iteration
        [[nodiscard]] task<void> worker(const std::uint32_t worker_id,
                        std::shared_ptr<connection_stats> stats) noexcept(false);

        /// @brief Worker sender coroutine
        [[nodiscard]] task<void> worker_sender(std::shared_ptr<tcp::stream> stream, const std::uint32_t worker_id,
                               std::shared_ptr<connection_stats> stats) noexcept(false);

        /// @brief Periodically renders live connection stats UI
        void ui_loop(std::stop_token stop_token) const;

        /// @brief Create or fetch stats entry for a connection
        std::shared_ptr<connection_stats> create_connection_stats(const std::uint32_t worker_id);

        /// @brief Mark connection closed if both TX and RX are inactive
        static void update_closed_state(const std::shared_ptr<connection_stats>& stats);

        /// @brief Print test summary
        void print_summary(const std::chrono::milliseconds elapsed) const;

        config config_;
        std::shared_ptr<executor> executor_;
        metric_data metrics_;

        mutable std::mutex connections_mutex_;
        std::unordered_map<std::uint32_t, std::shared_ptr<connection_stats>> connections_;
        std::jthread ui_thread_;
    };
}
