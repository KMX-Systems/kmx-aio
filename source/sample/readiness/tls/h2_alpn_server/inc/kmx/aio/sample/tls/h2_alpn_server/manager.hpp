#pragma once
#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/readiness/tcp/listener.hpp>
#include <kmx/aio/readiness/tcp/stream.hpp>
#include <kmx/aio/readiness/tls/stream.hpp>
#include <kmx/logger.hpp>

#include <atomic>
#include <csignal>
#include <memory>
#include <string>

namespace kmx::aio::sample::tls::h2_alpn_readiness_server
{
    struct config
    {
        std::uint32_t executor_threads = 4u;
        kmx::aio::ip_address_t bind_address = kmx::aio::make_ip_address(kmx::aio::localhost_ipv4);
        port_t bind_port = 8446u;
        port_t max_events = 1024u;
        port_t timeout_ms = 200u;
        std::string cert_file = "/tmp/cert.pem";
        std::string key_file = "/tmp/key.pem";
    };

    struct metric_data
    {
        std::atomic_uint64_t successes {};
        std::atomic_uint64_t failures {};
        std::atomic_uint64_t errors {};
    };

    class manager
    {
    public:
        explicit manager(config config = {}): config_(std::move(config)) {}
        [[nodiscard]] bool run() noexcept(false);

    private:
        [[nodiscard]] task<void> listener_task() noexcept(false);
        [[nodiscard]] task<void> handle_client(kmx::aio::readiness::tls::stream client_stream) noexcept(false);

        static void signal_handler(int signum) noexcept;

        config config_;
        std::shared_ptr<kmx::aio::readiness::executor> executor_;
        ::SSL_CTX* ssl_ctx_ {};
        metric_data metrics_ {};

        static inline std::atomic<kmx::aio::readiness::executor*> g_executor_ptr {};
    };

} // namespace kmx::aio::sample::tls::h2_alpn_readiness_server
