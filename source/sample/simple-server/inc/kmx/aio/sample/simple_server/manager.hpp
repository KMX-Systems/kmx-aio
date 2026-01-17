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
#include <unistd.h>

namespace kmx::aio::sample::simple_server
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

    // Server statistics
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
        /// @brief Handles a single client connection asynchronously
        [[nodiscard]] task<void> handle_client(tcp::stream stream, const std::uint64_t client_id) noexcept(false);

        /// @brief Accepts incoming connections and spawns handler coroutines
        [[nodiscard]] task<void> connection_acceptor() noexcept(false);

        /// @brief Print server statistics
        void print_statistics() const;

        /// @brief Signal handler for graceful shutdown
        static void signal_handler(int signum) noexcept;

        config config_;
        std::shared_ptr<executor> executor_;
        metric_data metrics_;

        static inline std::atomic<executor*> g_executor_ptr {};
    };

} // namespace kmx::aio::sample::server
