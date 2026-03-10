#pragma once
#include <kmx/aio/executor.hpp>
#include <kmx/aio/task.hpp>
#include <kmx/aio/udp/endpoint.hpp>
#include <kmx/logger.hpp>

#include <atomic>
#include <csignal>
#include <format>
#include <iostream>
#include <memory>
#include <string_view>
#include <unistd.h>

namespace kmx::aio::sample::udp::minimal::server
{
    // Server configuration
    struct config
    {
        std::uint32_t scheduler_threads = 4u;
        kmx::aio::ip_address_t bind_address = kmx::aio::make_ip_address(kmx::aio::localhost_ipv4);
        port_t bind_port = 9000u;
        port_t epoll_max_events = 1024u;
        port_t epoll_timeout_ms = 200u;
    };

    // Server statistics
    struct metric_data
    {
        std::atomic_uint64_t bytes_received {};
        std::atomic_uint64_t bytes_sent {};
        std::atomic_uint64_t messages_handled {};
        std::atomic_uint64_t errors {};
    };

    /// @brief UDP Echo server manager class
    class manager
    {
    public:
        explicit manager(config config = {}): config_(std::move(config)) {}

        const metric_data& metrics() const noexcept { return metrics_; }

        /// @brief Run the server
        [[nodiscard]] bool run() noexcept(false);

    private:
        /// @brief Main UDP server loop coroutine
        [[nodiscard]] task<void> server_loop() noexcept(false);

        /// @brief Print server statistics
        void print_statistics() const;

        /// @brief Signal handler for graceful shutdown
        static void signal_handler(int signum) noexcept;

        config config_;
        std::shared_ptr<executor> executor_;
        metric_data metrics_;

        static inline std::atomic<executor*> g_executor_ptr {};
    };

} // namespace kmx::aio::sample::udp::minimal::server
