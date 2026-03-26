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
#include <span>
#include <sys/socket.h>
#include <system_error>

extern "C"
{
#include <lsquic.h>
}

#include <kmx/aio/basic_types.hpp>
#include <kmx/aio/quic/settings.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::quic
{
    /// @brief Common QUIC engine implementation shared between readiness and completion models.
    /// @tparam Executor  The executor type (readiness::executor or completion::executor).
    /// @tparam UdpSocket The UDP socket type (readiness::udp::socket or completion::udp::socket).
    template <typename Executor, typename UdpSocket>
    struct base_impl
    {
        Executor& exec_;
        std::function<task<void>(std::span<char>)> stream_handler_;
        std::unique_ptr<UdpSocket> socket_;
        ::lsquic_engine_t* lsquic_engine_ {};
        sockaddr_storage local_addr_ {};
        void* ssl_ctx_ {};
        bool running_ {};

        explicit base_impl(Executor& exec) noexcept: exec_(exec) {}

        ~base_impl() noexcept
        {
            if (lsquic_engine_)
                ::lsquic_engine_destroy(lsquic_engine_);

            ::lsquic_global_cleanup();
        }

        // lsquic C callbacks

        static int send_packets_out(void* ctx, const struct ::lsquic_out_spec* specs, unsigned count)
        {
            auto* self = static_cast<base_impl*>(ctx);
            unsigned sent {};

            for (; sent < count; ++sent)
            {
                struct msghdr msg
                {
                };
                msg.msg_name = const_cast<void*>(reinterpret_cast<const void*>(specs[sent].dest_sa));
                msg.msg_namelen = (specs[sent].dest_sa->sa_family == AF_INET) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);

                iovec iov[1];
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

        static ::lsquic_conn_ctx_t* on_new_conn(void* stream_if_ctx, ::lsquic_conn_t* /*conn*/)
        {
            return reinterpret_cast<::lsquic_conn_ctx_t*>(stream_if_ctx);
        }

        static void on_conn_closed(::lsquic_conn_t* /*conn*/) {}

        static ::lsquic_stream_ctx_t* on_new_stream(void* /*stream_if_ctx*/, ::lsquic_stream_t* stream)
        {
            ::lsquic_stream_wantread(stream, 1);
            return nullptr;
        }

        static void on_read(::lsquic_stream_t* stream, ::lsquic_stream_ctx_t* /*ctx*/)
        {
            auto* self = reinterpret_cast<base_impl*>(::lsquic_conn_get_ctx(::lsquic_stream_conn(stream)));
            std::array<char, 4096> buf {};
            const ssize_t nr = ::lsquic_stream_read(stream, buf.data(), buf.size());
            if (nr > 0)
            {
                if (self->stream_handler_)
                    self->exec_.spawn(self->stream_handler_(std::span<char>(buf.data(), nr)));
            }
            else if (nr == 0)
                ::lsquic_stream_close(stream);
        }

        static void on_write(::lsquic_stream_t* stream, ::lsquic_stream_ctx_t* /*ctx*/) { ::lsquic_stream_wantwrite(stream, 0); }

        static struct ssl_ctx_st* get_ssl_ctx(void* peer_ctx, const struct sockaddr* /*local*/)
        {
            auto* self = static_cast<base_impl*>(peer_ctx);
            return reinterpret_cast<struct ssl_ctx_st*>(self->ssl_ctx_);
        }

        static void on_close(::lsquic_stream_t* /*stream*/, ::lsquic_stream_ctx_t* /*ctx*/) {}

        // Shared initialisation

        /// @brief Configures lsquic callbacks, settings, and creates the lsquic_engine.
        /// @return Success or an error code.
        [[nodiscard]] std::expected<void, std::error_code> init_lsquic(const kmx::aio::quic::settings& config)
        {
            if (::lsquic_global_init(LSQUIC_GLOBAL_SERVER) != 0)
                return std::unexpected(error_from_errno(EINVAL));

            static ::lsquic_stream_if stream_if {};
            stream_if.on_new_conn = on_new_conn;
            stream_if.on_conn_closed = on_conn_closed;
            stream_if.on_new_stream = on_new_stream;
            stream_if.on_read = on_read;
            stream_if.on_write = on_write;
            stream_if.on_close = on_close;

            ::lsquic_engine_api engine_api {};
            engine_api.ea_packets_out = send_packets_out;
            engine_api.ea_packets_out_ctx = this;
            engine_api.ea_stream_if = &stream_if;
            engine_api.ea_stream_if_ctx = this;
            engine_api.ea_get_ssl_ctx = get_ssl_ctx;

            static ::lsquic_engine_settings lsquic_settings {};
            ::lsquic_engine_init_settings(&lsquic_settings, LSENG_SERVER);
            lsquic_settings.es_max_streams_in = config.max_streams_in;
            lsquic_settings.es_idle_timeout = config.idle_conn_timeout_sec;
            lsquic_settings.es_max_cfcw = config.max_cfcwnd;
            engine_api.ea_settings = &lsquic_settings;

            lsquic_engine_ = ::lsquic_engine_new(LSENG_SERVER, &engine_api);
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

            local_addr_ = sock_addr_result->storage;
            if (::bind(socket_->get_fd(), reinterpret_cast<sockaddr*>(&sock_addr_result->storage), sock_addr_result->length) < 0)
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

            if (auto init_res = init_lsquic(config); !init_res)
                return std::unexpected(init_res.error());

            return {};
        }

        /// @brief Shared event processing loop.
        task<std::expected<void, std::error_code>> process()
        {
            running_ = true;
            std::array<std::byte, 4096> packet_buf {};

            while (running_)
            {
                ::lsquic_engine_process_conns(lsquic_engine_);

                struct msghdr msg
                {
                };
                struct iovec iov[1];
                sockaddr_storage peer_addr {};

                iov[0].iov_base = packet_buf.data();
                iov[0].iov_len = packet_buf.size();
                msg.msg_name = &peer_addr;
                msg.msg_namelen = sizeof(peer_addr);
                msg.msg_iov = iov;
                msg.msg_iovlen = 1;

                auto recv_res = co_await socket_->recvmsg(&msg);
                if (recv_res && *recv_res > 0)
                {
                    int diff = ::lsquic_engine_packet_in(lsquic_engine_, reinterpret_cast<const unsigned char*>(packet_buf.data()),
                                                         *recv_res, reinterpret_cast<sockaddr*>(&local_addr_),
                                                         reinterpret_cast<sockaddr*>(&peer_addr), reinterpret_cast<void*>(this), 0);
                    (void) diff;
                }
                else if (!recv_res)
                    co_return std::unexpected(recv_res.error());
            }

            co_return std::expected<void, std::error_code> {};
        }
    };

} // namespace kmx::aio::quic
