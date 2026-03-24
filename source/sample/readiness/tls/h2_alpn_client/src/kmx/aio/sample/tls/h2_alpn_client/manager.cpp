#include "kmx/aio/sample/tls/h2_alpn_client/manager.hpp"

#include <array>
#include <csignal>
#include <openssl/ssl.h>
#include <span>
#include <sys/socket.h>
#include <vector>

namespace kmx::aio::sample::tls::h2_alpn_readiness_client
{
    static constexpr auto mem_order = std::memory_order_relaxed;

    bool manager::run() noexcept(false)
    {
        const auto server_ip = kmx::aio::ip_to_string(config_.server_addr);
        logger::log(logger::level::info, std::source_location::current(), "H2 Readiness Client connecting to {}:{}", server_ip,
                    config_.server_port);

        std::signal(SIGPIPE, SIG_IGN);

        ssl_ctx_ = ::SSL_CTX_new(TLS_client_method());
        if (!ssl_ctx_)
            return false;

        ::SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_NONE, nullptr);

        readiness::executor_config exec_config {
            .thread_count = config_.scheduler_threads,
            .max_events = config_.max_events,
            .timeout_ms = config_.timeout_ms,
        };

        executor_ = std::make_shared<readiness::executor>(exec_config);

        for (std::uint32_t i = 0u; i < config_.num_workers; ++i)
        {
            executor_->spawn(worker(i));
        }

        executor_->run();

        ::SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;

        return metrics_.failures.load(mem_order) == 0u;
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

    task<std::expected<readiness::tls::stream, std::error_code>> manager::async_connect() noexcept
    {
        auto fd_result = create_nonblocking_socket();
        if (!fd_result)
            co_return std::unexpected(fd_result.error());

        auto fd_owner = std::move(*fd_result);
        const auto fd = fd_owner.get();

        auto addr_result = make_socket_address(config_.server_addr, config_.server_port);
        if (!addr_result)
            co_return std::unexpected(addr_result.error());

        auto connect_result = fd_owner.connect(reinterpret_cast<const sockaddr*>(&addr_result->storage), addr_result->length);
        const bool in_progress = (!connect_result && connect_result.error().value() == EINPROGRESS);

        if (!connect_result && !in_progress)
            co_return std::unexpected(connect_result.error());

        if (const auto reg_result = executor_->register_fd(fd); !reg_result)
            co_return std::unexpected(reg_result.error());

        if (in_progress)
            co_await executor_->wait_io(fd, event_type::write);

        int so_error = 0;
        ::socklen_t len = sizeof(so_error);
        if (auto sockopt_result = fd_owner.getsockopt(SOL_SOCKET, SO_ERROR, &so_error, &len); !sockopt_result)
        {
            executor_->unregister_fd(fd);
            co_return std::unexpected(sockopt_result.error());
        }

        if (so_error != 0)
        {
            executor_->unregister_fd(fd);
            co_return std::unexpected(std::error_code(so_error, std::generic_category()));
        }

        co_return readiness::tls::stream(readiness::tcp::stream(*executor_, std::move(fd_owner)), ssl_ctx_);
    }

    task<void> manager::worker(const std::uint32_t worker_id) noexcept(false)
    {
        metrics_.total_connections.fetch_add(1u, mem_order);

        try
        {
            auto stream_result = co_await async_connect();
            if (!stream_result)
            {
                metrics_.failures.fetch_add(1u, mem_order);
                metrics_.errors.fetch_add(1u, mem_order);
                metrics_.completed.fetch_add(1u, mem_order);
                co_return;
            }

            auto stream_ptr = std::make_shared<readiness::tls::stream>(std::move(*stream_result));
            stream_ptr->set_connect_state();

            const std::array<std::uint8_t, 3> alpn_h2 {2u, static_cast<std::uint8_t>('h'), static_cast<std::uint8_t>('2')};
            if (const auto alpn_res = stream_ptr->set_alpn_protocols(alpn_h2); !alpn_res)
            {
                metrics_.failures.fetch_add(1u, mem_order);
                metrics_.errors.fetch_add(1u, mem_order);
                metrics_.completed.fetch_add(1u, mem_order);
                co_return;
            }

            if (auto hs = co_await stream_ptr->handshake(); !hs)
            {
                metrics_.failures.fetch_add(1u, mem_order);
                metrics_.errors.fetch_add(1u, mem_order);
                metrics_.completed.fetch_add(1u, mem_order);
                co_return;
            }

            if (stream_ptr->selected_alpn() != "h2")
            {
                metrics_.failures.fetch_add(1u, mem_order);
                metrics_.completed.fetch_add(1u, mem_order);
                co_return;
            }

            std::string preface_data = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
            const std::array<char, 9> settings_frame {0, 0, 0, 4, 0, 0, 0, 0, 0};
            preface_data.append(settings_frame.data(), settings_frame.size());

            if (auto res = co_await stream_ptr->write_all(std::span<const char>(preface_data.data(), preface_data.size())); !res)
            {
                metrics_.failures.fetch_add(1u, mem_order);
                metrics_.errors.fetch_add(1u, mem_order);
                metrics_.completed.fetch_add(1u, mem_order);
                co_return;
            }

            std::array<char, 9> recv_buf {};
            auto r_res = co_await stream_ptr->read(std::span<char>(recv_buf.data(), recv_buf.size()));
            if (!r_res || *r_res < recv_buf.size() || recv_buf[3] != 4 || recv_buf[4] != 0)
            {
                metrics_.failures.fetch_add(1u, mem_order);
                metrics_.errors.fetch_add(1u, mem_order);
                metrics_.completed.fetch_add(1u, mem_order);
                co_return;
            }

            const std::array<char, 9> ack_frame {0, 0, 0, 4, 1, 0, 0, 0, 0};
            if (auto w_res = co_await stream_ptr->write_all(std::span<const char>(ack_frame.data(), ack_frame.size())); !w_res)
            {
                metrics_.failures.fetch_add(1u, mem_order);
                metrics_.errors.fetch_add(1u, mem_order);
                metrics_.completed.fetch_add(1u, mem_order);
                co_return;
            }

            r_res = co_await stream_ptr->read(std::span<char>(recv_buf.data(), recv_buf.size()));
            if (!r_res || *r_res < recv_buf.size() || recv_buf[3] != 4 || recv_buf[4] != 1)
            {
                metrics_.failures.fetch_add(1u, mem_order);
                metrics_.errors.fetch_add(1u, mem_order);
                metrics_.completed.fetch_add(1u, mem_order);
                co_return;
            }

            const char req_frame[] = {
                0x00, 0x00, 0x0e,
                0x01,
                0x05,
                0x00, 0x00, 0x00, 0x01,
                static_cast<char>(0x82), static_cast<char>(0x87), static_cast<char>(0x84),
                0x41, 0x09, 'l', 'o', 'c', 'a', 'l', 'h', 'o', 's', 't',
            };
            if (auto res = co_await stream_ptr->write_all(std::span<const char>(req_frame, sizeof(req_frame))); !res)
            {
                metrics_.failures.fetch_add(1u, mem_order);
                metrics_.errors.fetch_add(1u, mem_order);
                metrics_.completed.fetch_add(1u, mem_order);
                co_return;
            }

            std::array<char, 10> resp_hdr {};
            std::size_t total = 0u;
            while (total < resp_hdr.size())
            {
                auto r = co_await stream_ptr->read(std::span<char>(resp_hdr.data() + total, resp_hdr.size() - total));
                if (!r || *r == 0u)
                    break;
                total += *r;
            }

            std::array<char, 9> data_hdr {};
            total = 0u;
            while (total < data_hdr.size())
            {
                auto r = co_await stream_ptr->read(std::span<char>(data_hdr.data() + total, data_hdr.size() - total));
                if (!r || *r == 0u)
                    break;
                total += *r;
            }

            if (total == data_hdr.size() && data_hdr[3] == 0x00)
            {
                const auto data_len = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data_hdr[0])) << 16u) |
                                      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data_hdr[1])) << 8u) |
                                      static_cast<std::uint32_t>(static_cast<std::uint8_t>(data_hdr[2]));

                std::vector<char> data_payload(data_len);
                total = 0u;
                while (total < data_len)
                {
                    auto r = co_await stream_ptr->read(std::span<char>(data_payload.data() + total, data_len - total));
                    if (!r || *r == 0u)
                        break;
                    total += *r;
                }
            }

            ::shutdown(stream_ptr->inner().get_fd(), SHUT_WR);
            metrics_.successes.fetch_add(1u, mem_order);
        }
        catch (...)
        {
            metrics_.failures.fetch_add(1u, mem_order);
            metrics_.errors.fetch_add(1u, mem_order);
        }

        metrics_.completed.fetch_add(1u, mem_order);
        logger::log(logger::level::info, std::source_location::current(), "Client [{}] completed", worker_id);
        co_return;
    }
} // namespace kmx::aio::sample::tls::h2_alpn_readiness_client
