/// @file aio/quic/base_engine.hpp
/// @brief Shared QUIC engine implementation factored out of the readiness and completion models.
/// @details This is a PRIVATE implementation detail — included only from the .cpp files.
///          It must NOT appear in any public header to avoid exposing lsquic.h to consumers.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <netinet/in.h>
#include <queue>
#include <source_location>
#include <span>
#include <string>
#include <sys/socket.h>
#include <system_error>
#include <vector>

extern "C"
{
#include <lsquic.h>
}

#include <kmx/aio/basic_types.hpp>
#include <kmx/aio/quic/settings.hpp>
#include <kmx/aio/task.hpp>
#include <kmx/logger.hpp>

namespace kmx::aio::quic
{
    /// @brief Common QUIC engine implementation shared between readiness and completion models.
    /// @tparam Executor  The executor type (readiness::executor or completion::executor).
    /// @tparam UdpSocket The UDP socket type (readiness::udp::socket or completion::udp::socket).
    template <typename Executor, typename UdpSocket>
    struct base_impl
    {
        using connection_status_t = ::LSQUIC_CONN_STATUS;

        static constexpr std::size_t stream_payload_pool_capacity = 1024u;

        Executor& exec_;
        std::function<task<void>(::lsquic_stream_t*, stream_payload)> stream_handler_;
        kmx::aio::buffer_pool<stream_payload_buffer, stream_payload_pool_capacity> stream_payload_pool_ {};
        std::unique_ptr<UdpSocket> socket_;
        ::lsquic_engine_t* lsquic_engine_ {};
        sockaddr_storage local_addr_ {};
        void* ssl_ctx_ {};
        bool running_ {};
        bool is_client_ {false};
        std::queue<std::string> client_payloads_ {};

        explicit base_impl(Executor& exec) noexcept: exec_(exec) {}

        [[nodiscard]] static std::string_view conn_status_to_string(const connection_status_t status) noexcept
        {
            switch (status)
            {
                case LSCONN_ST_HSK_IN_PROGRESS: return "LSCONN_ST_HSK_IN_PROGRESS";
                case LSCONN_ST_CONNECTED: return "LSCONN_ST_CONNECTED";
                case LSCONN_ST_HSK_FAILURE: return "LSCONN_ST_HSK_FAILURE";
                case LSCONN_ST_GOING_AWAY: return "LSCONN_ST_GOING_AWAY";
                case LSCONN_ST_TIMED_OUT: return "LSCONN_ST_TIMED_OUT";
                case LSCONN_ST_RESET: return "LSCONN_ST_RESET";
                case LSCONN_ST_USER_ABORTED: return "LSCONN_ST_USER_ABORTED";
                case LSCONN_ST_ERROR: return "LSCONN_ST_ERROR";
                case LSCONN_ST_CLOSED: return "LSCONN_ST_CLOSED";
                case LSCONN_ST_PEER_GOING_AWAY: return "LSCONN_ST_PEER_GOING_AWAY";
                case LSCONN_ST_VERNEG_FAILURE: return "LSCONN_ST_VERNEG_FAILURE";
                default: return "LSCONN_ST_UNKNOWN";
            }
        }

        ~base_impl() noexcept
        {
            if (lsquic_engine_)
                ::lsquic_engine_destroy(lsquic_engine_);

            ::lsquic_global_cleanup();
        }

        // lsquic C callbacks

        static int send_packets_out(void* ctx, const struct ::lsquic_out_spec* specs, unsigned count)
        {
            auto* const self = static_cast<base_impl*>(ctx);
            unsigned sent {};
            ::msghdr msg {};
            ::iovec iov[1u] {};

            for (; sent < count; ++sent)
            {
                msg = {};
                msg.msg_name = const_cast<void*>(reinterpret_cast<const void*>(specs[sent].dest_sa));
                msg.msg_namelen = (specs[sent].dest_sa->sa_family == AF_INET) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);

                iov[0].iov_base = const_cast<void*>(specs[sent].iov[0].iov_base);
                iov[0].iov_len = specs[sent].iov[0].iov_len;
                msg.msg_iov = iov;
                msg.msg_iovlen = 1;

                const ssize_t res = ::sendmsg(self->socket_->get_fd(), &msg, 0);
                if ((res < 0) && would_block(errno))
                    break;
            }

            return static_cast<int>(sent);
        }

        static ::lsquic_conn_ctx_t* on_new_conn(void* stream_if_ctx, ::lsquic_conn_t* conn)
        {
            auto* const self = static_cast<base_impl*>(stream_if_ctx);
            if (self->is_client_)
            {
                const std::size_t streams_to_open = self->client_payloads_.size();
                for (std::size_t i = 0; i < streams_to_open; ++i)
                    ::lsquic_conn_make_stream(conn);
            }
            return reinterpret_cast<::lsquic_conn_ctx_t*>(stream_if_ctx);
        }

        static void on_conn_closed(::lsquic_conn_t* conn)
        {
            auto* const self = reinterpret_cast<base_impl*>(::lsquic_conn_get_ctx(conn));
            std::array<char, 512u> errbuf {};
            const auto status = ::lsquic_conn_status(conn, errbuf.data(), errbuf.size());
            logger::log(logger::level::info, std::source_location::current(),
                        "[QUIC DEBUG] on_conn_closed called, status={} ({}), reason='{}'",
                        static_cast<int>(status), conn_status_to_string(status), errbuf.data());

            if (self && self->is_client_)
                self->running_ = false;

            ::lsquic_conn_set_ctx(conn, nullptr);
        }

        static void on_hsk_done(::lsquic_conn_t* conn, enum lsquic_hsk_status status)
        {
            auto* const self = reinterpret_cast<base_impl*>(::lsquic_conn_get_ctx(conn));
            logger::log(logger::level::info, std::source_location::current(),
                        "[QUIC DEBUG] on_hsk_done called, status={}, is_client_={}",
                        static_cast<int>(status), self ? self->is_client_ : false);

            if (self && self->is_client_)
            {
                logger::log(logger::level::info, std::source_location::current(),
                            "[QUIC DEBUG] on_hsk_done: client handshake completed");
            }
        }

        static ::lsquic_stream_ctx_t* on_new_stream(void* stream_if_ctx, ::lsquic_stream_t* stream)
        {
            auto* const self = static_cast<base_impl*>(stream_if_ctx);
            if (self->is_client_)
                ::lsquic_stream_wantwrite(stream, 1);
            else
                ::lsquic_stream_wantread(stream, 1);
            return reinterpret_cast<::lsquic_stream_ctx_t*>(stream_if_ctx);
        }

        static void on_read(::lsquic_stream_t* stream, ::lsquic_stream_ctx_t* /*ctx*/)
        {
            auto* const self = reinterpret_cast<base_impl*>(::lsquic_conn_get_ctx(::lsquic_stream_conn(stream)));

            auto handle_read_result = [&](const ssize_t nr) -> void
            {
                if (nr == 0)
                    ::lsquic_stream_close(stream);
            };

            if (!self->stream_handler_)
            {
                std::array<char, stream_payload_capacity> scratch {};
                handle_read_result(::lsquic_stream_read(stream, scratch.data(), scratch.size()));
                return;
            }

            buffer_handle<stream_payload_buffer> payload_storage;
            try
            {
                payload_storage = self->stream_payload_pool_.acquire();
            }
            catch (const std::exception&)
            {
                std::array<char, stream_payload_capacity> scratch {};
                const ssize_t nr = ::lsquic_stream_read(stream, scratch.data(), scratch.size());
                if (nr > 0)
                {
                    logger::log(logger::level::warn,
                                std::source_location::current(),
                                "QUIC payload pool exhausted; dropping {} byte(s)",
                                static_cast<std::size_t>(nr));
                    return;
                }

                handle_read_result(nr);
                return;
            }

            const ssize_t nr = ::lsquic_stream_read(stream, payload_storage->data(), payload_storage->size());
            if (nr > 0)
            {
                self->exec_.spawn(self->stream_handler_(stream, stream_payload {std::move(payload_storage), static_cast<std::size_t>(nr)}));
                return;
            }

            handle_read_result(nr);
        }

        static void on_write(::lsquic_stream_t* stream, ::lsquic_stream_ctx_t* /*ctx*/)
        {
            auto* const self = reinterpret_cast<base_impl*>(::lsquic_conn_get_ctx(::lsquic_stream_conn(stream)));
            if (self->is_client_ && !self->client_payloads_.empty())
            {
                std::string payload = std::move(self->client_payloads_.front());
                self->client_payloads_.pop();

                std::size_t written {};
                while (written < payload.size())
                {
                    const ssize_t chunk = ::lsquic_stream_write(stream, payload.data() + written, payload.size() - written);
                    if (chunk <= 0)
                    {
                        logger::log(logger::level::warn,
                                    std::source_location::current(),
                                    "QUIC client write failed on stream {}, written={}/{}",
                                    static_cast<unsigned long long>(::lsquic_stream_id(stream)),
                                    written,
                                    payload.size());
                        break;
                    }

                    written += static_cast<std::size_t>(chunk);
                }

                ::lsquic_stream_flush(stream);
                ::lsquic_stream_wantwrite(stream, 0);
                ::lsquic_stream_wantread(stream, 1);
            }
            else
                ::lsquic_stream_wantwrite(stream, 0);
        }

        static struct ssl_ctx_st* get_ssl_ctx(void* peer_ctx, const struct sockaddr* /*local*/)
        {
            auto* const self = static_cast<base_impl*>(peer_ctx);
            return reinterpret_cast<struct ssl_ctx_st*>(self->ssl_ctx_);
        }

        static struct ssl_ctx_st* lookup_cert(void* cert_lu_ctx, const struct sockaddr* /*local*/, const char* /*sni*/)
        {
            auto* const self = static_cast<base_impl*>(cert_lu_ctx);
            return reinterpret_cast<struct ssl_ctx_st*>(self->ssl_ctx_);
        }

        static void on_close(::lsquic_stream_t* /*stream*/, ::lsquic_stream_ctx_t* /*ctx*/) {}

        // Shared initialisation

        /// @brief Configures lsquic callbacks, settings, and creates the lsquic_engine.
        /// @return Success or an error code.
        [[nodiscard]] std::expected<void, std::error_code> init_lsquic(const kmx::aio::quic::settings& config, unsigned lsquic_flags)
        {
            if (::lsquic_global_init(lsquic_flags & LSENG_SERVER ? LSQUIC_GLOBAL_SERVER : LSQUIC_GLOBAL_CLIENT) != 0)
                return std::unexpected(error_from_errno(EINVAL));

            static ::lsquic_stream_if stream_if {};
            stream_if.on_new_conn = on_new_conn;
            stream_if.on_conn_closed = on_conn_closed;
            stream_if.on_new_stream = on_new_stream;
            stream_if.on_read = on_read;
            stream_if.on_write = on_write;
            stream_if.on_close = on_close;
            stream_if.on_hsk_done = on_hsk_done;

            ::lsquic_engine_api engine_api {};
            engine_api.ea_packets_out = send_packets_out;
            engine_api.ea_packets_out_ctx = this;
            engine_api.ea_stream_if = &stream_if;
            engine_api.ea_stream_if_ctx = this;
            engine_api.ea_lookup_cert = lookup_cert;
            engine_api.ea_cert_lu_ctx = this;
            engine_api.ea_get_ssl_ctx = get_ssl_ctx;
            engine_api.ea_alpn = "kmx-aio";

            static ::lsquic_engine_settings lsquic_settings {};
            ::lsquic_engine_init_settings(&lsquic_settings, lsquic_flags);
            lsquic_settings.es_max_streams_in = config.max_streams_in;
            lsquic_settings.es_idle_timeout = config.idle_conn_timeout_sec;
            lsquic_settings.es_max_cfcw = config.max_cfcwnd;
            engine_api.ea_settings = &lsquic_settings;

            lsquic_engine_ = ::lsquic_engine_new(lsquic_flags, &engine_api);
            if (!lsquic_engine_)
                return std::unexpected(error_from_errno(EINVAL));

            return {};
        }

        /// @brief Binds the UDP socket and stores the local address.
        /// @return Success or an error code.
        [[nodiscard]] std::expected<void, std::error_code> bind_socket(const ip_address_t ip, const port_t port)
        {
            auto sock_addr_result = make_socket_address(ip, port);
            if (!sock_addr_result)
                return std::unexpected(sock_addr_result.error());

            if (::bind(socket_->get_fd(), reinterpret_cast<sockaddr*>(&sock_addr_result->storage), sock_addr_result->length) < 0)
                return std::unexpected(error_from_errno());

            // For ephemeral binds (port 0), propagate the kernel-assigned local address to lsquic.
            ::socklen_t local_len = sizeof(local_addr_);
            if (::getsockname(socket_->get_fd(), reinterpret_cast<sockaddr*>(&local_addr_), &local_len) < 0)
                return std::unexpected(error_from_errno());

            return {};
        }

        /// @brief Shared initialisation logic called after model-specific socket creation.
        [[nodiscard]] std::expected<void, std::error_code> setup(std::expected<UdpSocket, std::error_code>&& sock_res,
                                                                 const ip_address_t ip, const port_t port, void* ssl_ctx,
                                                                 const kmx::aio::quic::settings& config)
        {
            if (!sock_res)
                return std::unexpected(sock_res.error());

            ssl_ctx_ = ssl_ctx;
            socket_ = std::make_unique<UdpSocket>(std::move(*sock_res));

            if (auto bind_res = bind_socket(ip, port); !bind_res)
                return std::unexpected(bind_res.error());

            if (auto init_res = init_lsquic(config, LSENG_SERVER); !init_res)
                return std::unexpected(init_res.error());

            return {};
        }

        [[nodiscard]] std::expected<void, std::error_code> connect_setup(std::expected<UdpSocket, std::error_code>&& sock_res,
                                                                         const ip_address_t peer_ip, const port_t peer_port,
                                                                         const std::string& hostname, const std::string& client_payload,
                                                                         void* ssl_ctx, const kmx::aio::quic::settings& config)
        {
            if (!sock_res)
                return std::unexpected(sock_res.error());

            ssl_ctx_ = ssl_ctx;
            is_client_ = true;
            while (!client_payloads_.empty())
                client_payloads_.pop();

            if (!client_payload.empty())
                client_payloads_.push(client_payload);
            socket_ = std::make_unique<UdpSocket>(std::move(*sock_res));

            // Bind to ephemeral port
            static constexpr std::array<uint8_t, 4u> any_ip {0, 0, 0, 0};
            if (auto bind_res = bind_socket(any_ip, 0); !bind_res)
                return std::unexpected(bind_res.error());

            if (auto init_res = init_lsquic(config, 0); !init_res)
                return std::unexpected(init_res.error());

            auto peer_addr_result = make_socket_address(peer_ip, peer_port);
            if (!peer_addr_result)
                return std::unexpected(peer_addr_result.error());

            if (::connect(socket_->get_fd(), reinterpret_cast<sockaddr*>(&peer_addr_result->storage), peer_addr_result->length) < 0)
                return std::unexpected(error_from_errno());

            ::socklen_t local_len = sizeof(local_addr_);
            if (::getsockname(socket_->get_fd(), reinterpret_cast<sockaddr*>(&local_addr_), &local_len) < 0)
                return std::unexpected(error_from_errno());

            const char* host = hostname.empty() ? nullptr : hostname.c_str();

            ::lsquic_conn_t* const conn = ::lsquic_engine_connect(lsquic_engine_, N_LSQVER, reinterpret_cast<sockaddr*>(&local_addr_),
                                                                  reinterpret_cast<sockaddr*>(&peer_addr_result->storage),
                                                                  static_cast<void*>(this), nullptr, host, 0, nullptr, 0, nullptr, 0);
            if (!conn)
                return std::unexpected(error_from_errno());

            return {};
        }

        [[nodiscard]] std::expected<void, std::error_code> connect_setup(std::expected<UdpSocket, std::error_code>&& sock_res,
                                                                         const ip_address_t peer_ip, const port_t peer_port,
                                                                         const std::string& hostname,
                                                                         const std::vector<std::string>& client_payloads,
                                                                         void* ssl_ctx, const kmx::aio::quic::settings& config)
        {
            if (!sock_res)
                return std::unexpected(sock_res.error());

            ssl_ctx_ = ssl_ctx;
            is_client_ = true;
            while (!client_payloads_.empty())
                client_payloads_.pop();

            for (const auto& payload: client_payloads)
            {
                if (!payload.empty())
                    client_payloads_.push(payload);
            }

            socket_ = std::make_unique<UdpSocket>(std::move(*sock_res));

            // Bind to ephemeral port
            static constexpr std::array<uint8_t, 4u> any_ip {0, 0, 0, 0};
            if (auto bind_res = bind_socket(any_ip, 0); !bind_res)
                return std::unexpected(bind_res.error());

            if (auto init_res = init_lsquic(config, 0); !init_res)
                return std::unexpected(init_res.error());

            auto peer_addr_result = make_socket_address(peer_ip, peer_port);
            if (!peer_addr_result)
                return std::unexpected(peer_addr_result.error());

            if (::connect(socket_->get_fd(), reinterpret_cast<sockaddr*>(&peer_addr_result->storage), peer_addr_result->length) < 0)
                return std::unexpected(error_from_errno());

            ::socklen_t local_len = sizeof(local_addr_);
            if (::getsockname(socket_->get_fd(), reinterpret_cast<sockaddr*>(&local_addr_), &local_len) < 0)
                return std::unexpected(error_from_errno());

            const char* host = hostname.empty() ? nullptr : hostname.c_str();

            ::lsquic_conn_t* const conn = ::lsquic_engine_connect(lsquic_engine_, N_LSQVER, reinterpret_cast<sockaddr*>(&local_addr_),
                                                                  reinterpret_cast<sockaddr*>(&peer_addr_result->storage),
                                                                  static_cast<void*>(this), nullptr, host, 0, nullptr, 0, nullptr, 0);
            if (!conn)
                return std::unexpected(error_from_errno());

            return {};
        }

        /// @brief Shared event processing loop.
        task<std::expected<void, std::error_code>> process()
        {
            running_ = true;
            std::array<std::byte, 4096u> packet_buf {};
            ::msghdr msg {};
            ::iovec iov[1u] {};

            // Do initial lsquic processing to emit Initial packets before recvmsg-driven processing.
            for (int i = 0; i < 10; ++i)
            {
                ::lsquic_engine_process_conns(lsquic_engine_);
                ::lsquic_engine_send_unsent_packets(lsquic_engine_);
            }

            while (running_)
            {
                ::lsquic_engine_process_conns(lsquic_engine_);
                ::lsquic_engine_send_unsent_packets(lsquic_engine_);

                ::sockaddr_storage peer_addr {};

                iov[0].iov_base = packet_buf.data();
                iov[0].iov_len = packet_buf.size();
                msg.msg_name = &peer_addr;
                msg.msg_namelen = sizeof(peer_addr);
                msg.msg_iov = iov;
                msg.msg_iovlen = 1;

                if constexpr (requires (Executor& e) { e.async_timeout(std::uint64_t {}); })
                {
                    const ssize_t recv_n = ::recvmsg(socket_->get_fd(), &msg, MSG_DONTWAIT);
                    if (recv_n < 0)
                    {
                        if (would_block(errno))
                        {
                            // Keep driving lsquic timers while no UDP datagram is currently available.
                            auto timeout_res = co_await exec_.async_timeout(1'000'000ULL); // 1 ms
                            if (!timeout_res)
                                co_return std::unexpected(timeout_res.error());
                            continue;
                        }

                        co_return std::unexpected(error_from_errno());
                    }

                    if (recv_n > 0)
                    {
                        const int packet_in_res =
                            ::lsquic_engine_packet_in(lsquic_engine_, reinterpret_cast<const unsigned char*>(packet_buf.data()),
                                                      static_cast<std::size_t>(recv_n), reinterpret_cast<::sockaddr*>(&local_addr_),
                                                      reinterpret_cast<::sockaddr*>(&peer_addr), reinterpret_cast<void*>(this), 0);
                        if (packet_in_res < 0)
                        {
                            logger::log(logger::level::error, std::source_location::current(), "lsquic_engine_packet_in failed: {}",
                                        packet_in_res);
                            co_return std::unexpected(error_from_errno(EPROTO));
                        }

                        ::lsquic_engine_process_conns(lsquic_engine_);
                        ::lsquic_engine_send_unsent_packets(lsquic_engine_);
                    }
                }
                else
                {
                    auto recv_res = co_await socket_->recvmsg(&msg);
                    if (!recv_res)
                    {
                        logger::log(logger::level::error, std::source_location::current(),
                                    "[QUIC DEBUG] recvmsg error: {}", recv_res.error().message());
                        co_return std::unexpected(recv_res.error());
                    }

                    if (*recv_res > 0)
                    {
                        const int packet_in_res =
                            ::lsquic_engine_packet_in(lsquic_engine_, reinterpret_cast<const unsigned char*>(packet_buf.data()), *recv_res,
                                                      reinterpret_cast<::sockaddr*>(&local_addr_), reinterpret_cast<::sockaddr*>(&peer_addr),
                                                      reinterpret_cast<void*>(this), 0);
                        if (packet_in_res < 0)
                        {
                            logger::log(logger::level::error, std::source_location::current(), "lsquic_engine_packet_in failed: {}",
                                        packet_in_res);
                            co_return std::unexpected(error_from_errno(EPROTO));
                        }

                        ::lsquic_engine_process_conns(lsquic_engine_);
                        ::lsquic_engine_send_unsent_packets(lsquic_engine_);
                    }
                }
            }

            co_return std::expected<void, std::error_code> {};
        }
    };

} // namespace kmx::aio::quic
