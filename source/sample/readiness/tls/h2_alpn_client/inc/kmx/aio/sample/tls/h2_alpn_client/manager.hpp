#pragma once
#include <kmx/aio/file_descriptor.hpp>
#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/readiness/tcp/stream.hpp>
#include <kmx/aio/readiness/tls/stream.hpp>
#include <kmx/aio/task.hpp>
#include <kmx/logger.hpp>
#include <openssl/ssl.h>

#include <atomic>
#include <expected>
#include <memory>
#include <string_view>
#include <system_error>

namespace kmx::aio::sample::tls::h2_alpn_readiness_client
{
    struct config
    {
        std::uint32_t num_workers = 1u;
        kmx::aio::ip_address_t server_addr = kmx::aio::make_ip_address(kmx::aio::localhost_ipv4);
        port_t server_port = 8446u;
        std::uint32_t scheduler_threads = 2u;
        port_t max_events = 1024u;
        port_t timeout_ms = 100u;
    };

    struct metric_data
    {
        std::atomic_size_t successes {};
        std::atomic_size_t failures {};
        std::atomic_size_t total_connections {};
        std::atomic_size_t completed {};
        std::atomic_uint64_t errors {};
    };

    class manager
    {
    public:
        explicit manager(config config = {}): config_(std::move(config)) {}

        [[nodiscard]] bool run() noexcept(false);
        [[nodiscard]] const metric_data& metrics() const noexcept { return metrics_; }

    private:
        [[nodiscard]] std::expected<file_descriptor, std::error_code> create_nonblocking_socket() noexcept;
        [[nodiscard]] task<std::expected<readiness::tls::stream, std::error_code>> async_connect() noexcept;
        [[nodiscard]] task<void> worker(std::uint32_t worker_id) noexcept(false);

        config config_;
        std::shared_ptr<readiness::executor> executor_;
        metric_data metrics_ {};
        ::SSL_CTX* ssl_ctx_ {};
    };
} // namespace kmx::aio::sample::tls::h2_alpn_readiness_client
