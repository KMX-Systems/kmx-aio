#include <kmx/aio/descriptor/file.hpp>
#include <kmx/aio/executor.hpp>
#include <kmx/aio/task.hpp>
#include <kmx/aio/tcp/listener.hpp>
#include <kmx/aio/tcp/stream.hpp>
#include <kmx/logger.hpp>

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <expected>
#include <fcntl.h>
#include <format>
#include <iostream>
#include <sys/socket.h>

namespace kmx::aio::sample::client
{
    // Configuration
    struct config
    {
        std::uint32_t num_workers = 1000;
        std::string_view server_addr = "127.0.0.1";
        std::uint16_t server_port = 8080;
        std::string_view message = "Is there anybody out there?";
        std::uint32_t scheduler_threads = 4u;
        std::uint16_t max_events = 1024u;
        std::uint16_t timeout_ms = 100u;
    };

    // Metrics
    struct metrics
    {
        std::atomic_size_t successes {0u};
        std::atomic_size_t failures {0u};
        std::atomic_size_t total_connections {0u};
        std::atomic_size_t completed {0u};
    };

    /// @brief Stress test manager class
    class manager
    {
    public:
        explicit manager(config config = {}): config_(std::move(config)) {}

        /// @brief Run the stress test
        [[nodiscard]] int run() noexcept(false);

    private:
        /// @brief Creates a non-blocking socket
        [[nodiscard]] static std::expected<kmx::aio::descriptor::file, std::error_code> create_nonblocking_socket() noexcept;

        /// @brief Asynchronously connects to the server and returns a TCP stream
        [[nodiscard]] kmx::aio::task<std::expected<kmx::aio::tcp::stream, std::error_code>> async_connect() noexcept;

        /// @brief Worker coroutine that performs an async stress test iteration
        [[nodiscard]] kmx::aio::task<void> worker(const int worker_id) noexcept(false);

        /// @brief Print test summary
        void print_summary(const std::chrono::milliseconds elapsed) const;

        config config_;
        std::shared_ptr<kmx::aio::executor> executor_;
        metrics metrics_;
    };
}
