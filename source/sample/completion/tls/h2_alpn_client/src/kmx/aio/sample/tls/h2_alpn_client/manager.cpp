#include "kmx/aio/sample/tls/h2_alpn_client/manager.hpp"
#include "kmx/aio/sample/tcp/echo/common.hpp"

#include <algorithm>
#include <iomanip>
#include <span>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <vector>

namespace kmx::aio::sample::tls::h2_alpn_client
{
    static constexpr auto mem_order = std::memory_order_relaxed;

    bool manager::run() noexcept(false)
    {
        const auto server_ip = kmx::aio::ip_to_string(config_.server_addr);
        logger::log(logger::level::info, std::source_location::current(), "H2 Client connecting to {}:{}", server_ip, config_.server_port);

        std::signal(SIGPIPE, SIG_IGN);
        ssl_ctx_ = ::SSL_CTX_new(TLS_client_method());
        ::SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_NONE, nullptr);

        const unsigned char alpn_protos[] = {2, 'h', '2'};
        ::SSL_CTX_set_alpn_protos(ssl_ctx_, alpn_protos, sizeof(alpn_protos));

        const completion::executor_config exec_config {.thread_count = config_.scheduler_threads};
        executor_ = std::make_shared<completion::executor>(exec_config);

        for (std::uint32_t i {}; i < config_.num_workers; ++i)
        {
            executor_->spawn(worker(i, nullptr));
        }

        executor_->run();
        return metrics_.failures.load(mem_order) == 0;
    }

    std::expected<file_descriptor, std::error_code> manager::create_nonblocking_socket() noexcept
    {
        auto fd_result = file_descriptor::create_socket(ip_family(config_.server_addr), SOCK_STREAM, 0);
        if (!fd_result)
            return std::unexpected(fd_result.error());
        auto fd = std::move(*fd_result);

        auto flags_result = fd.fcntl(F_GETFL);
        if (!flags_result)
            return std::unexpected(flags_result.error());
        auto setfl_result = fd.fcntl(F_SETFL, *flags_result | O_NONBLOCK);
        if (!setfl_result)
            return std::unexpected(setfl_result.error());

        return fd;
    }

    task<std::expected<kmx::aio::completion::tls::stream, std::error_code>> manager::async_connect() noexcept
    {
        auto fd_result = create_nonblocking_socket();
        if (!fd_result)
            co_return std::unexpected(fd_result.error());

        auto fd_owner = std::move(*fd_result);
        const auto fd = fd_owner.get();

        auto addr_result = make_socket_address(config_.server_addr, config_.server_port);
        if (!addr_result)
            co_return std::unexpected(addr_result.error());

        auto r = co_await executor_->async_connect(fd, reinterpret_cast<const sockaddr*>(&addr_result->storage), addr_result->length);
        if (!r)
            co_return std::unexpected(r.error());

        co_return kmx::aio::completion::tls::stream(kmx::aio::completion::tcp::stream(executor_, std::move(fd_owner)), ssl_ctx_);
    }

    task<void> manager::worker(const std::uint32_t worker_id, std::shared_ptr<connection_stats> stats) noexcept(false)
    {
        (void) stats;
        try
        {
            auto stream_result = co_await async_connect();
            if (!stream_result)
                co_return;

            auto stream_ptr = std::make_shared<kmx::aio::completion::tls::stream>(std::move(*stream_result));
            stream_ptr->set_connect_state();
            if (auto hs = co_await stream_ptr->handshake(); !hs)
                co_return;

            const unsigned char* alpn_data = nullptr;
            unsigned int alpn_len = 0;
            ::SSL_get0_alpn_selected(stream_ptr->native_handle(), &alpn_data, &alpn_len);

            if (alpn_len == 2 && alpn_data[0] == 'h' && alpn_data[1] == '2')
            {
                logger::log(logger::level::info, std::source_location::current(), "Client [{}]: ALPN h2 negotiated", worker_id);
            }
            else
            {
                co_return;
            }

            // Send Preface + Empty SETTINGS Frame
            std::string preface_data = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
            char settings_frame[9] = {0, 0, 0, 4, 0, 0, 0, 0, 0}; // length=0, type=4 (SETTINGS), flags=0, stream_id=0
            preface_data.append(settings_frame, 9);

            if (auto res = co_await stream_ptr->write_all(std::span<const char>(preface_data.data(), preface_data.size())); !res)
            {
                co_return;
            }
            logger::log(logger::level::info, std::source_location::current(), "Client [{}]: Sent Preface + SETTINGS", worker_id);

            // Read Server SETTINGS frame
            char recv_buf[9];
            auto r_res = co_await stream_ptr->read(std::span<char>(recv_buf, 9));
            if (!r_res || *r_res < 9)
                co_return;

            if (recv_buf[3] == 4 && recv_buf[4] == 0)
            { // type == SETTINGS, flags == 0
                logger::log(logger::level::info, std::source_location::current(), "Client [{}]: Received Server SETTINGS", worker_id);
            }

            // Send SETTINGS ACK
            char ack_frame[9] = {0, 0, 0, 4, 1, 0, 0, 0, 0}; // flags=1 (ACK)
            if (auto w_res = co_await stream_ptr->write_all(std::span<const char>(ack_frame, 9)); !w_res)
            {
                co_return;
            }
            logger::log(logger::level::info, std::source_location::current(), "Client [{}]: Sent SETTINGS ACK", worker_id);

            // Read Server SETTINGS ACK
            r_res = co_await stream_ptr->read(std::span<char>(recv_buf, 9));
            if (r_res && *r_res >= 9 && recv_buf[3] == 4 && recv_buf[4] == 1)
            { // type == SETTINGS, flags == 1
                logger::log(logger::level::info, std::source_location::current(),
                            "Client [{}]: Received Server SETTINGS ACK. Handshake Complete!", worker_id);
            }

            // HTTP/2 Extension: Send a static HPACK encoded GET request
            char req_frame[] = {0x00,        0x00,        0x0e,              // Length: 14
                                0x01,                                        // Type: HEADERS
                                0x05,                                        // Flags: END_HEADERS | END_STREAM
                                0x00,        0x00,        0x00,        0x01, // Stream ID: 1
                                (char) 0x82, (char) 0x87, (char) 0x84,       // GET, https, /
                                0x41,        0x09,        'l',         'o',  'c', 'a', 'l', 'h', 'o', 's', 't'};
            if (auto res = co_await stream_ptr->write_all(std::span<const char>(req_frame, sizeof(req_frame))); !res)
                co_return;
            logger::log(logger::level::info, std::source_location::current(), "Client [{}]: Sent GET Request (Stream 1)", worker_id);

            // Read Response HEADERS Frame
            char resp_hdr[10];
            size_t total = 0;
            while (total < 10)
            {
                auto r = co_await stream_ptr->read(std::span<char>(resp_hdr + total, 10 - total));
                if (!r || *r == 0)
                    break;
                total += *r;
            }
            if (total == 10)
            {
                logger::log(logger::level::info, std::source_location::current(), "Client [{}]: Received Response HEADERS. Status: {}",
                            worker_id, (resp_hdr[9] == (char) 0x88 ? "200 OK" : "Unknown"));
            }

            // Read DATA Frame
            char data_hdr[9];
            total = 0;
            while (total < 9)
            {
                auto r = co_await stream_ptr->read(std::span<char>(data_hdr + total, 9 - total));
                if (!r || *r == 0)
                    break;
                total += *r;
            }
            if (total == 9 && data_hdr[3] == 0x00)
            { // Type == DATA
                uint32_t data_len = (static_cast<uint8_t>(data_hdr[0]) << 16) | (static_cast<uint8_t>(data_hdr[1]) << 8) |
                                    static_cast<uint8_t>(data_hdr[2]);
                std::vector<char> data_payload(data_len + 1, ' ');
                total = 0;
                while (total < data_len)
                {
                    auto r = co_await stream_ptr->read(std::span<char>(data_payload.data() + total, data_len - total));
                    if (!r || *r == 0)
                        break;
                    total += *r;
                }
                logger::log(logger::level::info, std::source_location::current(), "Client [{}]: Received DATA: {}", worker_id,
                            data_payload.data());
            }

            ::shutdown(stream_ptr->inner().get_fd(), SHUT_WR);
        }
        catch (...)
        {
        }
        co_return;
    }

    kmx::aio::task<void> manager::worker_sender(std::shared_ptr<kmx::aio::completion::tls::stream> stream, const std::uint32_t worker_id,
                                                std::shared_ptr<connection_stats> stats) noexcept(false)
    {
        co_return;
    }

    std::shared_ptr<manager::connection_stats> manager::create_connection_stats(const std::uint32_t worker_id)
    {
        return {};
    }

    void manager::update_closed_state(const std::shared_ptr<connection_stats>& stats)
    {
    }

    void manager::ui_loop(std::stop_token stop_token) const
    {
    }

    void manager::print_summary(const std::chrono::milliseconds elapsed) const
    {
    }
}
