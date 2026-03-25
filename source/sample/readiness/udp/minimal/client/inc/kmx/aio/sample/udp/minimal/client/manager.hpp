#pragma once
#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/readiness/udp/socket.hpp>
#include <kmx/aio/task.hpp>
#include <kmx/logger.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <string_view>

namespace kmx::aio::sample::udp::minimal::client
{
    // Configuration
    struct config
    {
        std::uint32_t num_workers = 1000u;
        kmx::aio::ip_address_t server_addr = kmx::aio::make_ip_address(kmx::aio::localhost_ipv4);
        port_t server_port = 9000u;
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
        std::atomic_size_t total_requests {};
        std::atomic_size_t completed {};
    };

    /// @brief UDP Stress test manager class
    class manager
    {
    public:
        explicit manager(config config = {}): config_(std::move(config)) {}

        const metric_data& metrics() const noexcept { return metrics_; }

        /// @brief Run the stress test.
        /// @return true when all requests succeed; otherwise false.
        [[nodiscard]] bool run() noexcept(false);

    private:
        /// @brief Worker coroutine that performs an async stress test iteration
        [[nodiscard]] task<void> worker(const std::uint32_t worker_id) noexcept(false);

        /// @brief Print test summary
        void print_summary(const std::chrono::milliseconds elapsed) const;

        config config_;
        std::shared_ptr<readiness::executor> executor_;
        metric_data metrics_;
    };
}
