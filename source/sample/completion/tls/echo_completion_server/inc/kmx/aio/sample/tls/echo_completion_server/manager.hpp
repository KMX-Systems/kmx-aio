#pragma once
#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/tcp/listener.hpp>
#include <kmx/aio/completion/tcp/stream.hpp>
#include <kmx/aio/completion/tls/stream.hpp>
#include <kmx/logger.hpp>

#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace kmx::aio::sample::tls::echo_completion_server
{
    struct config
    {
        std::uint32_t executor_threads = 4u;
        kmx::aio::ip_address_t bind_address = kmx::aio::make_ip_address(kmx::aio::localhost_ipv4);
        port_t bind_port = 8444u;
        port_t max_events = 1024u;
        port_t timeout_ms = 200u;
        std::string cert_file = "/tmp/cert.pem";
        std::string key_file = "/tmp/key.pem";
    };

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
        [[nodiscard]] task<void> listener_task() noexcept(false);
        [[nodiscard]] task<void> handle_client(kmx::aio::completion::tls::stream client_stream) noexcept(false);

        void ui_loop(std::stop_token stop_token) const;
        void print_metrics() const;
        static void signal_handler(int signum) noexcept;

        config config_;
        std::shared_ptr<kmx::aio::completion::executor> executor_;
        ::SSL_CTX* ssl_ctx_ {};
        metric_data metrics_;
        std::jthread ui_thread_;

        static inline std::atomic<kmx::aio::completion::executor*> g_executor_ptr {};
    };

} // namespace kmx::aio::sample::tls::echo_completion_server
