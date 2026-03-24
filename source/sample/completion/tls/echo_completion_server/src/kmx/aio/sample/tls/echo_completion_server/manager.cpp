#include "kmx/aio/sample/tls/echo_completion_server/manager.hpp"

#include <chrono>
#include <format>
#include <iostream>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <span>

namespace kmx::aio::sample::tls::echo_completion_server
{
    static constexpr std::size_t transfer_limit_bytes = 200u * 1024u;

    bool manager::run() noexcept(false)
    {
        logger::log(logger::level::info, std::source_location::current(), "TLS Completion Server Starting");

        std::signal(SIGPIPE, SIG_IGN);
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        // Intialize OpenSSL Context
        ssl_ctx_ = ::SSL_CTX_new(TLS_server_method());
        if (!ssl_ctx_)
        {
            logger::log(logger::level::error, std::source_location::current(), "Failed to create SSL_CTX");
            return false;
        }

        if (::SSL_CTX_use_certificate_chain_file(ssl_ctx_, config_.cert_file.c_str()) <= 0)
        {
            logger::log(logger::level::error, std::source_location::current(), "Failed to load cert");
            ::SSL_CTX_free(ssl_ctx_);
            return false;
        }

        if (::SSL_CTX_use_PrivateKey_file(ssl_ctx_, config_.key_file.c_str(), SSL_FILETYPE_PEM) <= 0)
        {
            logger::log(logger::level::error, std::source_location::current(), "Failed to load key");
            ::SSL_CTX_free(ssl_ctx_);
            return false;
        }

        kmx::aio::completion::executor_config exec_config {.ring_entries = config_.max_events,
                                                           .max_completions = config_.max_events,
                                                           .thread_count = config_.executor_threads,
                                                           .core_id = -1};

        executor_ = std::make_shared<kmx::aio::completion::executor>(exec_config);
        g_executor_ptr.store(executor_.get(), std::memory_order_release);

        executor_->spawn(listener_task());

        ui_thread_ = std::jthread([this](std::stop_token stop_token) { ui_loop(stop_token); });
        executor_->run();

        if (ui_thread_.joinable())
        {
            ui_thread_.request_stop();
            ui_thread_.join();
        }

        ::SSL_CTX_free(ssl_ctx_);
        print_metrics();
        return metrics_.errors == 0u;
    }

    kmx::aio::task<void> manager::listener_task() noexcept(false)
    {
        auto listener = kmx::aio::completion::tcp::listener(executor_, config_.bind_address, config_.bind_port);
        if (!listener.listen(128))
        {
            co_return;
        }

        while (true)
        {
            auto accept_result = co_await listener.accept();
            if (!accept_result)
            {
                metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
                continue;
            }

            metrics_.total_connections.fetch_add(1u, std::memory_order_relaxed);

            auto tcp_stream = kmx::aio::completion::tcp::stream(executor_, std::move(*accept_result));
            auto tls_stream = kmx::aio::completion::tls::stream(std::move(tcp_stream), ssl_ctx_);

            executor_->spawn(handle_client(std::move(tls_stream)));
        }
    }

    kmx::aio::task<void> manager::handle_client(kmx::aio::completion::tls::stream stream) noexcept(false)
    {
        metrics_.active_connections.fetch_add(1u, std::memory_order_relaxed);
        std::vector<char> buffer(65535u);

        try
        {
            stream.set_accept_state();
            auto handshake_result = co_await stream.handshake();
            if (!handshake_result)
            {
                metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
                metrics_.active_connections.fetch_sub(1u, std::memory_order_relaxed);
                co_return;
            }

            std::size_t total_received = 0;

            while (true)
            {
                auto read_result = co_await stream.read(std::span<char>(buffer));
                if (!read_result)
                {
                    if (read_result.error().value() != 0)
                        metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
                    break;
                }

                const auto bytes_read = *read_result;
                if (bytes_read == 0)
                    break;

                total_received += bytes_read;
                metrics_.bytes_received.fetch_add(bytes_read, std::memory_order_relaxed);

                std::span<const char> write_span(buffer.data(), bytes_read);
                auto write_result = co_await stream.write(write_span);

                if (!write_result)
                {
                    metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
                    break;
                }

                metrics_.bytes_sent.fetch_add(bytes_read, std::memory_order_relaxed);
                if (total_received >= transfer_limit_bytes)
                    break;
            }
        }
        catch (const std::exception&)
        {
            metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
        }

        metrics_.active_connections.fetch_sub(1u, std::memory_order_relaxed);
        co_return;
    }

    void manager::ui_loop(std::stop_token stop_token) const
    {
        using namespace std::chrono_literals;

        while (!stop_token.stop_requested())
        {
            const auto total = metrics_.total_connections.load(std::memory_order_relaxed);
            const auto active = metrics_.active_connections.load(std::memory_order_relaxed);
            const auto errors = metrics_.errors.load(std::memory_order_relaxed);

            std::cout << "\x1b[2J\x1b[H";
            std::cout << "Live TLS Completion Stats\n";
            std::cout << "────────────────────────────────────────────────────────────────────────\n";
            std::cout << std::format("Connections: Active {} | Total {} | Errors: {}\n", active, total, errors);
            std::cout << std::flush;

            std::this_thread::sleep_for(250ms);
        }
    }

    void manager::print_metrics() const
    {
        std::cout << "\nShutdown Metrics for TLS Completion\n";
        std::cout << std::format("Total Connections: {}\n", metrics_.total_connections.load(std::memory_order_relaxed));
        std::cout << std::format("Errors: {}\n", metrics_.errors.load(std::memory_order_relaxed));
    }

    void manager::signal_handler(int signum) noexcept
    {
        if (signum == SIGINT || signum == SIGTERM)
        {
            ::write(STDERR_FILENO, "[SIGNAL] Stopping TLS executor\n", 31);
            auto* exec = g_executor_ptr.load(std::memory_order_acquire);
            if (exec)
                exec->stop();
        }
    }
}
