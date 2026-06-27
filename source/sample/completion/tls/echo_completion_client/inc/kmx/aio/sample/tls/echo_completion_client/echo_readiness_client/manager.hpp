#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/tcp/listener.hpp>
#include <kmx/aio/completion/tcp/stream.hpp>
#include <kmx/aio/completion/tls/stream.hpp>
#include <kmx/aio/file_descriptor.hpp>
#include <kmx/aio/task.hpp>
#include <kmx/logger.hpp>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <expected>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <stop_token>
#include <sys/socket.h>
#include <thread>
#include <unordered_map>

namespace kmx::aio::sample::tls::echo_completion_client
{
    // Configuration
    struct config
    {
        std::uint32_t num_workers = 1000u;
        kmx::aio::ip_address_t server_addr = kmx::aio::make_ip_address(kmx::aio::localhost_ipv4);
        port_t server_port = 8443u;
        std::string_view message = "Is there anybody out there?";
        std::uint32_t scheduler_threads = 4u;
        port_t max_events = 1024u;
        port_t timeout_ms = 100u;
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
        /// @return true when all requests succeed; otherwise false.
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
        [[nodiscard]] std::expected<file_descriptor, std::error_code> create_nonblocking_socket() noexcept;

        /// @brief Asynchronously connects to the server and returns a TCP stream
        [[nodiscard]] task<std::expected<completion::tls::stream, std::error_code>> async_connect() noexcept;

        /// @brief Worker coroutine that performs an async stress test iteration
        [[nodiscard]] task<void> worker(const std::uint32_t worker_id, std::shared_ptr<connection_stats> stats) noexcept(false);

        /// @brief Worker sender coroutine
        [[nodiscard]] task<void> worker_sender(std::shared_ptr<completion::tls::stream> stream, const std::uint32_t worker_id,
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
        std::shared_ptr<completion::executor> executor_;
        metric_data metrics_;
        ::SSL_CTX* ssl_ctx_ {};

        mutable std::mutex connections_mutex_;
        std::unordered_map<std::uint32_t, std::shared_ptr<connection_stats>> connections_;
        std::jthread ui_thread_;
    };
}
