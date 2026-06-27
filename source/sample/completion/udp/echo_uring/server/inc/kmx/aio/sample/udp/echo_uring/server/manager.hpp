#pragma once
#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/udp/socket.hpp>
#include <kmx/logger.hpp>

#include <atomic>
#include <csignal>
#include <format>
#include <iostream>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace kmx::aio::sample::udp::echo_uring::server
{
    struct config
    {
        std::uint32_t executor_threads = 4u;
        std::uint32_t listener_workers = 4u;
        kmx::aio::ip_address_t bind_address = kmx::aio::make_ip_address(kmx::aio::localhost_ipv4);
        port_t bind_port = 8080u;
        port_t max_events = 1024u;
        port_t timeout_ms = 200u;
    };

    struct metric_data
    {
        std::atomic_uint64_t messages_handled {};
        std::atomic_uint64_t bytes_received {};
        std::atomic_uint64_t bytes_sent {};
        std::atomic_uint64_t errors {};
    };

    class manager
    {
    public:
        explicit manager(config config = {}): config_(std::move(config)) {}

        [[nodiscard]] bool run() noexcept(false);

    private:
        [[nodiscard]] task<void> listener(const std::uint32_t worker_id) noexcept(false);

        void ui_loop(std::stop_token stop_token) const;
        void print_statistics() const;
        static void signal_handler(int signum) noexcept;

        config config_;
        std::shared_ptr<kmx::aio::completion::executor> executor_;
        metric_data metrics_;
        std::jthread ui_thread_;

        static inline std::atomic<kmx::aio::completion::executor*> g_executor_ptr {};
    };

} // namespace kmx::aio::sample::udp::echo_uring::server
