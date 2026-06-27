#pragma once
#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/readiness/tcp/listener.hpp>
#include <kmx/aio/readiness/tcp/stream.hpp>
#include <kmx/logger.hpp>

#include <atomic>
#include <memory>
#include <unistd.h>

namespace kmx::aio::sample::tcp::minimal::server
{
    // Server configuration
    struct config
    {
        std::uint32_t scheduler_threads = 4u;
        kmx::aio::ip_address_t bind_address = kmx::aio::make_ip_address(kmx::aio::localhost_ipv4);
        port_t bind_port = 8080u;
        port_t epoll_max_events = 1024u;
        port_t epoll_timeout_ms = 200u;
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
        [[nodiscard]] task<void> handle_client(kmx::aio::readiness::tcp::stream stream, const std::uint64_t client_id) noexcept(false);

        /// @brief Accepts incoming connections and spawns handler coroutines
        [[nodiscard]] task<void> connection_acceptor() noexcept(false);

        /// @brief Print server statistics
        void print_statistics() const;

        /// @brief Signal handler for graceful shutdown
        static void signal_handler(int signum) noexcept;

        config config_;
        std::shared_ptr<readiness::executor> executor_;
        metric_data metrics_;

        static inline std::atomic<readiness::executor*> g_executor_ptr {};
    };

} // namespace kmx::aio::sample::tcp::minimal::server
