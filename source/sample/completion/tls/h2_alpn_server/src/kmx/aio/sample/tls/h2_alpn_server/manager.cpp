#include <kmx/aio/completion/tcp/listener.hpp>
#include <kmx/aio/sample/tls/h2_alpn_server/manager.hpp>

#include <array>
#include <csignal>
#include <format>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <span>
#include <vector>

namespace kmx::aio::sample::tls::h2_alpn_server
{
    static int alpn_select_cb(SSL*, const unsigned char** out, unsigned char* outlen, const unsigned char* in, unsigned inlen, void*)
    {
        static const unsigned char alpn_h2[] = {2, 'h', '2'};
        if (::SSL_select_next_proto((unsigned char**) out, outlen, alpn_h2, sizeof(alpn_h2), in, inlen) != OPENSSL_NPN_NEGOTIATED)
            return SSL_TLSEXT_ERR_NOACK;

        return SSL_TLSEXT_ERR_OK;
    }

    manager::~manager() noexcept
    {
        if (ssl_ctx_)
            ::SSL_CTX_free(ssl_ctx_);
    }

    bool manager::run() noexcept(false)
    {
        logger::log(logger::level::info, std::source_location::current(), "H2 Server Starting");
        std::signal(SIGPIPE, SIG_IGN);
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        ssl_ctx_ = ::SSL_CTX_new(TLS_server_method());
        ::SSL_CTX_set_alpn_select_cb(ssl_ctx_, alpn_select_cb, nullptr);

        if (::SSL_CTX_use_certificate_chain_file(ssl_ctx_, config_.cert_file.c_str()) <= 0)
            return false;
        if (::SSL_CTX_use_PrivateKey_file(ssl_ctx_, config_.key_file.c_str(), SSL_FILETYPE_PEM) <= 0)
            return false;

        kmx::aio::completion::executor_config exec_config {.ring_entries = config_.max_events,
                                                           .max_completions = config_.max_events,
                                                           .thread_count = config_.executor_threads,
                                                           .core_id = -1};

        executor_ = std::make_unique<kmx::aio::completion::executor>(exec_config);
        g_executor_ptr.store(executor_.get(), std::memory_order_release);

        executor_->spawn(listener_task());
        executor_->run();
        return true;
    }

    kmx::aio::task<void> manager::listener_task() noexcept(false)
    {
        auto listener = kmx::aio::completion::tcp::listener(*executor_, config_.bind_address, config_.bind_port);
        if (!listener.listen(128))
            co_return;

        while (true)
        {
            auto accept_result = co_await listener.accept();
            if (!accept_result)
                continue;
            auto tcp_stream = kmx::aio::completion::tcp::stream(*executor_, std::move(*accept_result));
            auto tls_stream = kmx::aio::completion::tls::stream(std::move(tcp_stream), ssl_ctx_);
            executor_->spawn(handle_client(std::move(tls_stream)));
        }
    }

    kmx::aio::task<void> manager::handle_client(kmx::aio::completion::tls::stream stream) noexcept(false)
    {
        try
        {
            stream.set_accept_state();
            if (auto handshake_result = co_await stream.handshake(); !handshake_result)
                co_return;

            if (stream.selected_alpn() == "h2")
                logger::log(logger::level::info, std::source_location::current(), "Server: ALPN h2 negotiated");
            else
                co_return;

            // Wait for 24 byte Preface + 9 byte Client Settings
            std::array<char, 33u> recv_buf {};
            auto read_res = co_await stream.read(span_char_t(recv_buf.data(), recv_buf.size()));
            if (!read_res || *read_res < recv_buf.size())
                co_return;

            std::string_view expected_preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
            std::string_view received(recv_buf.data(), 24);
            if (received == expected_preface && recv_buf[24 + 3] == 4)
                // type == SETTINGS
                logger::log(logger::level::info, std::source_location::current(), "Server: Received valid Preface + Client SETTINGS frame");

            // Send Server SETTINGS + SETTINGS ACK to Client
            static constexpr std::array<char, 18u> send_frames {
                0, 0, 0, 4, 0, 0, 0, 0, 0, // Server SETTINGS
                0, 0, 0, 4, 1, 0, 0, 0, 0  // SETTINGS ACK
            };

            if (auto w_res = co_await stream.write_all(cspan_char_t(send_frames)); !w_res)
                co_return;
            logger::log(logger::level::info, std::source_location::current(), "Server: Sent SETTINGS + SETTINGS ACK");

            // Read Client SETTINGS ACK
            auto r_ack = co_await stream.read(span_char_t(recv_buf.data(), 9u));
            if (r_ack && *r_ack >= 9 && recv_buf[3] == 4 && recv_buf[4] == 1)
                // type == SETTINGS, flags == 1
                logger::log(logger::level::info, std::source_location::current(),
                            "Server: Received Client SETTINGS ACK. Handshake Complete!");

            // HTTP/2 Extension: Listen for incoming GET packet and process HEADERS
            std::array<char, 9u> req_hdr {};
            size_t total {};
            while (total < req_hdr.size())
            {
                auto r = co_await stream.read(span_char_t(req_hdr.data() + total, req_hdr.size() - total));
                if (!r || *r == 0)
                    break;
                total += *r;
            }

            if ((total == req_hdr.size()) && (req_hdr[3] == 0x01))
            { // Type HEADERS
                uint32_t payload_len =
                    (static_cast<uint8_t>(req_hdr[0]) << 16) | (static_cast<uint8_t>(req_hdr[1]) << 8) | static_cast<uint8_t>(req_hdr[2]);
                std::vector<char> payload(payload_len);
                total = {};
                while (total < payload_len)
                {
                    auto r = co_await stream.read(span_char_t(payload.data() + total, payload_len - total));
                    if (!r || *r == 0)
                        break;
                    total += *r;
                }

                logger::log(logger::level::info, std::source_location::current(),
                            "Server: Received HEADERS map (Length: {}) -> Formulating Response", payload_len);

                // HEADERS frame (length 1, END_HEADERS, stream 1, :status 200) followed by a DATA
                // frame (length 21, END_STREAM, stream 1) carrying the response body.
                static constexpr auto resp = std::to_array<char>({
                    0x00, 0x00, 0x01, 0x01, 0x04, 0x00, 0x00, 0x00, 0x01, static_cast<char>(0x88),

                    0x00, 0x00, 0x15, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 'H',
                    'e',  'l',  'l',  'o',  ' ',  'f',  'r',  'o',  'm',  ' ',
                    'K',  'M',  'X',  ' ',  'H',  'T',  'T',  'P',  '/',  '2',
                });
                if (auto w_res = co_await stream.write_all(cspan_char_t(resp)); w_res)
                    logger::log(logger::level::info, std::source_location::current(), "Server: Sent 200 OK + DATA");
            }
        }
        catch (...)
        {
        }

        co_return;
    }

    void manager::ui_loop(std::stop_token) const
    {
    }

    void manager::print_metrics() const
    {
    }

    void manager::signal_handler(int signum) noexcept
    {
        if ((signum == SIGINT) || (signum == SIGTERM))
        {
            auto* exec = g_executor_ptr.load(std::memory_order_acquire);
            if (exec)
                exec->stop();
        }
    }
}
