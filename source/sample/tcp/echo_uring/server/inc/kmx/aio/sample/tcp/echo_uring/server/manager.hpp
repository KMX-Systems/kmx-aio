#pragma once
#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/tcp/listener.hpp>
#include <kmx/aio/completion/tcp/stream.hpp>
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
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace kmx::aio::sample::tcp::echo_uring::server
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

    // Server metrics
    struct metric_data
    {
        std::atomic_uint64_t total_connections {};
        std::atomic_uint64_t active_connections {};
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
        struct connection_stats
        {
            std::atomic_uint64_t bytes_received {};
            std::atomic_uint64_t bytes_sent {};
            std::atomic_uint64_t errors {};
            std::atomic_bool rx_active {false};
            std::atomic_bool tx_active {false};
            std::atomic_bool closed {false};
        };

        [[nodiscard]] task<void> handle_client(kmx::aio::completion::tcp::stream stream, const std::uint64_t client_id,
                                               std::shared_ptr<connection_stats> stats) noexcept(false);

        [[nodiscard]] task<void> client_sender(std::shared_ptr<kmx::aio::completion::tcp::stream> stream,
                                               const std::uint64_t client_id,
                                               std::shared_ptr<connection_stats> stats) noexcept(false);

        [[nodiscard]] task<void> connection_acceptor() noexcept(false);

        void ui_loop(std::stop_token stop_token) const;
        std::shared_ptr<connection_stats> create_connection_stats(const std::uint64_t client_id);
        static void update_closed_state(const std::shared_ptr<connection_stats>& stats);
        void print_metrics() const;
        static void signal_handler(int signum) noexcept;

        config config_;
        std::shared_ptr<kmx::aio::completion::executor> executor_;
        metric_data metrics_;

        mutable std::mutex connections_mutex_;
        std::unordered_map<std::uint64_t, std::shared_ptr<connection_stats>> connections_;
        std::jthread ui_thread_;

        static inline std::atomic<kmx::aio::completion::executor*> g_executor_ptr {};

        std::vector<::iovec> registered_buffers_;
        std::vector<char> buffer_memory_;
        std::mutex buf_mutex_;
        std::vector<int> free_buf_indices_;

        int allocate_buffer();
        void free_buffer(int index);
    };

} // namespace kmx::aio::sample::tcp::echo_uring::server
