#pragma once
#include <kmx/aio/executor.hpp>
#include <kmx/aio/tcp/listener.hpp>
#include <kmx/aio/tcp/stream.hpp>
#include <kmx/logger.hpp>

#include <atomic>
#include <csignal>
#include <format>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <unistd.h>

namespace kmx::aio::sample::server
{
    // Server configuration
    struct config
    {
        std::uint32_t scheduler_threads = 4u;
        std::string_view bind_address = "127.0.0.1";
        std::uint16_t bind_port = 8080u;
        std::uint16_t epoll_max_events = 1024u;
        std::uint16_t epoll_timeout_ms = 200u;
    };

    // Server metrics
    struct metric_data
    {
        std::atomic_uint64_t total_connections {};
        std::atomic_uint64_t active_connections {};
        std::atomic_uint64_t bytes_received {};
        std::atomic_uint64_t bytes_sent {};
        std::atomic_uint64_t errors {};
    };

    /// @brief Echo server manager class
    class manager
    {
    public:
        explicit manager(config config = {}): config_(std::move(config)) {}

        const metric_data& metrics() const noexcept { return metrics_; }

        /// @brief Run the server
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

        /// @brief Handles a single client connection asynchronously
        [[nodiscard]] task<void> handle_client(tcp::stream stream, const std::uint64_t client_id,
                                               std::shared_ptr<connection_stats> stats) noexcept(false);

        /// @brief Handles sending random data to client asynchronously
        [[nodiscard]] task<void> client_sender(std::shared_ptr<tcp::stream> stream, const std::uint64_t client_id,
                               std::shared_ptr<connection_stats> stats) noexcept(false);

        /// @brief Accepts incoming connections and spawns handler coroutines
        [[nodiscard]] task<void> connection_acceptor() noexcept(false);

        /// @brief Periodically renders live connection stats UI
        void ui_loop(std::stop_token stop_token) const;

        /// @brief Create or fetch stats entry for a connection
        std::shared_ptr<connection_stats> create_connection_stats(const std::uint64_t client_id);

        /// @brief Mark connection closed if both TX and RX are inactive
        static void update_closed_state(const std::shared_ptr<connection_stats>& stats);

        /// @brief Print server metrics
        void print_metrics() const;

        /// @brief Signal handler for graceful shutdown
        static void signal_handler(int signum) noexcept;

        config config_;
        std::shared_ptr<executor> executor_;
        metric_data metrics_;

        mutable std::mutex connections_mutex_;
        std::unordered_map<std::uint64_t, std::shared_ptr<connection_stats>> connections_;
        std::jthread ui_thread_;

        static inline std::atomic<executor*> g_executor_ptr {};
    };

} // namespace kmx::aio::sample::server
