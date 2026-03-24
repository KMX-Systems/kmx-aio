#include "kmx/aio/sample/tls/h2_alpn_server/manager.hpp"

#include <array>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <span>
#include <string_view>

namespace kmx::aio::sample::tls::h2_alpn_readiness_server
{
    static int alpn_select_cb(SSL*, const unsigned char** out, unsigned char* outlen, const unsigned char* in, unsigned int inlen, void*)
    {
        static const unsigned char alpn_h2[] = {2, 'h', '2'};
        if (::SSL_select_next_proto((unsigned char**) out, outlen, alpn_h2, sizeof(alpn_h2), in, inlen) != OPENSSL_NPN_NEGOTIATED)
        {
            return SSL_TLSEXT_ERR_NOACK;
        }
        return SSL_TLSEXT_ERR_OK;
    }

    bool manager::run() noexcept(false)
    {
        logger::log(logger::level::info, std::source_location::current(), "H2 Readiness Server Starting");

        std::signal(SIGPIPE, SIG_IGN);
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        ssl_ctx_ = ::SSL_CTX_new(TLS_server_method());
        if (!ssl_ctx_)
            return false;

        ::SSL_CTX_set_alpn_select_cb(ssl_ctx_, alpn_select_cb, nullptr);

        if (::SSL_CTX_use_certificate_chain_file(ssl_ctx_, config_.cert_file.c_str()) <= 0)
        {
            ::SSL_CTX_free(ssl_ctx_);
            return false;
        }

        if (::SSL_CTX_use_PrivateKey_file(ssl_ctx_, config_.key_file.c_str(), SSL_FILETYPE_PEM) <= 0)
        {
            ::SSL_CTX_free(ssl_ctx_);
            return false;
        }

        readiness::executor_config exec_config {
            .thread_count = config_.executor_threads,
            .max_events = config_.max_events,
            .timeout_ms = config_.timeout_ms,
        };

        executor_ = std::make_shared<readiness::executor>(exec_config);
        g_executor_ptr.store(executor_.get(), std::memory_order_release);

        executor_->spawn(listener_task());
        executor_->run();

        ::SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;

        return metrics_.failures.load(std::memory_order_relaxed) == 0u;
    }

    task<void> manager::listener_task() noexcept(false)
    {
        auto listener = readiness::tcp::listener(*executor_, config_.bind_address, config_.bind_port);
        if (!listener.listen(128))
            co_return;

        while (true)
        {
            auto accept_result = co_await listener.accept();
            if (!accept_result)
            {
                metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
                continue;
            }

            auto tcp_stream = readiness::tcp::stream(*executor_, std::move(*accept_result));
            auto tls_stream = readiness::tls::stream(std::move(tcp_stream), ssl_ctx_);
            executor_->spawn(handle_client(std::move(tls_stream)));
        }
    }

    task<void> manager::handle_client(kmx::aio::readiness::tls::stream stream) noexcept(false)
    {
        try
        {
            stream.set_accept_state();
            if (auto handshake_result = co_await stream.handshake(); !handshake_result)
            {
                metrics_.failures.fetch_add(1u, std::memory_order_relaxed);
                co_return;
            }

            if (stream.selected_alpn() != "h2")
            {
                metrics_.failures.fetch_add(1u, std::memory_order_relaxed);
                co_return;
            }

            std::array<char, 33> recv_buf {};
            auto read_res = co_await stream.read(std::span<char>(recv_buf.data(), recv_buf.size()));
            if (!read_res || *read_res < recv_buf.size())
            {
                metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
                co_return;
            }

            constexpr std::string_view expected_preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
            const std::string_view received_preface(recv_buf.data(), 24u);
            if (received_preface != expected_preface || recv_buf[24 + 3] != 4)
            {
                metrics_.failures.fetch_add(1u, std::memory_order_relaxed);
                co_return;
            }

            const std::array<char, 18> send_frames {
                0, 0, 0, 4, 0, 0, 0, 0, 0,
                0, 0, 0, 4, 1, 0, 0, 0, 0,
            };
            if (auto w_res = co_await stream.write_all(std::span<const char>(send_frames.data(), send_frames.size())); !w_res)
            {
                metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
                co_return;
            }

            std::array<char, 9> ack_buf {};
            auto r_ack = co_await stream.read(std::span<char>(ack_buf.data(), ack_buf.size()));
            if (!r_ack || *r_ack < ack_buf.size() || ack_buf[3] != 4 || ack_buf[4] != 1)
            {
                metrics_.failures.fetch_add(1u, std::memory_order_relaxed);
                co_return;
            }

            std::array<char, 9> req_hdr {};
            std::size_t total = 0u;
            while (total < req_hdr.size())
            {
                auto r = co_await stream.read(std::span<char>(req_hdr.data() + total, req_hdr.size() - total));
                if (!r || *r == 0u)
                    break;
                total += *r;
            }

            if (total == req_hdr.size() && req_hdr[3] == 0x01)
            {
                const auto payload_len = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(req_hdr[0])) << 16u) |
                                         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(req_hdr[1])) << 8u) |
                                         static_cast<std::uint32_t>(static_cast<std::uint8_t>(req_hdr[2]));

                std::vector<char> payload(payload_len);
                total = 0u;
                while (total < payload_len)
                {
                    auto r = co_await stream.read(std::span<char>(payload.data() + total, payload_len - total));
                    if (!r || *r == 0u)
                        break;
                    total += *r;
                }

                const char resp[] = {
                    0x00, 0x00, 0x01,
                    0x01,
                    0x04,
                    0x00, 0x00, 0x00, 0x01,
                    static_cast<char>(0x88),

                    0x00, 0x00, 0x15,
                    0x00,
                    0x01,
                    0x00, 0x00, 0x00, 0x01,
                    'H', 'e', 'l', 'l', 'o', ' ', 'f', 'r', 'o', 'm', ' ',
                    'K', 'M', 'X', ' ', 'H', 'T', 'T', 'P', '/', '2',
                };

                if (auto w_res = co_await stream.write_all(std::span<const char>(resp, sizeof(resp))); !w_res)
                {
                    metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
                    co_return;
                }
            }

            metrics_.successes.fetch_add(1u, std::memory_order_relaxed);
        }
        catch (...)
        {
            metrics_.errors.fetch_add(1u, std::memory_order_relaxed);
            metrics_.failures.fetch_add(1u, std::memory_order_relaxed);
        }

        co_return;
    }

    void manager::signal_handler(const int signum) noexcept
    {
        if (signum == SIGINT || signum == SIGTERM)
        {
            auto* exec = g_executor_ptr.load(std::memory_order_acquire);
            if (exec)
                exec->stop();
        }
    }
} // namespace kmx::aio::sample::tls::h2_alpn_readiness_server
