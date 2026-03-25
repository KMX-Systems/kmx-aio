/// @file aio/completion/quic/engine.cpp
/// @brief Completion-model QUIC engine implementation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#if defined(KMX_AIO_FEATURE_QUIC)

#ifndef PCH
    #include <arpa/inet.h>
    #include <iostream>
    #include <netinet/in.h>
    #include <sys/socket.h>

extern "C" {
    #include <lsquic.h>
}

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/completion/quic/engine.hpp>
    #include <kmx/aio/completion/udp/socket.hpp>
#endif

namespace kmx::aio::completion::quic
{
    struct engine::impl
    {
        executor& exec_;
        stream_handler_t stream_handler_;
        std::unique_ptr<udp::socket> socket_;
        lsquic_engine_t* lsquic_engine_ {nullptr};
        sockaddr_storage local_addr_ {};
        void* ssl_ctx_ {nullptr};
        bool running_ {false};

        explicit impl(executor& exec) noexcept : exec_(exec)
        {
        }

        ~impl() noexcept
        {
            if (lsquic_engine_)
            {
                ::lsquic_engine_destroy(lsquic_engine_);
            }
            ::lsquic_global_cleanup();
        }

        static int send_packets_out(void* ctx, const struct lsquic_out_spec* specs, unsigned count)
        {
            auto* self = static_cast<impl*>(ctx);
            unsigned sent = 0;

            for (; sent < count; ++sent)
            {
                struct msghdr msg {};
                msg.msg_name = const_cast<char*>(reinterpret_cast<const char*>(specs[sent].dest_sa));
                msg.msg_namelen = (specs[sent].dest_sa->sa_family == AF_INET) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);

                iovec iov[1];
                iov[0].iov_base = const_cast<char*>(reinterpret_cast<const char*>(specs[sent].iov[0].iov_base));
                iov[0].iov_len = specs[sent].iov[0].iov_len;
                msg.msg_iov = iov;
                msg.msg_iovlen = 1;

                const ssize_t res = ::sendmsg(self->socket_->get_fd(), &msg, 0);
                if (res < 0)
                {
                    if (would_block(errno))
                        break;
                }
            }
            return static_cast<int>(sent);
        }

        static lsquic_conn_ctx_t* on_new_conn(void* stream_if_ctx, lsquic_conn_t* /*conn*/)
        {
            return reinterpret_cast<lsquic_conn_ctx_t*>(stream_if_ctx);
        }

        static void on_conn_closed(lsquic_conn_t* /*conn*/)
        {
        }

        static lsquic_stream_ctx_t* on_new_stream(void* /*stream_if_ctx*/, lsquic_stream_t* stream)
        {
            ::lsquic_stream_wantread(stream, 1);
            return nullptr;
        }

        static void on_read(lsquic_stream_t* stream, lsquic_stream_ctx_t* /*ctx*/)
        {
            auto* self = reinterpret_cast<impl*>(::lsquic_conn_get_ctx(::lsquic_stream_conn(stream)));
            std::array<char, 4096> buf {};
            const ssize_t nr = ::lsquic_stream_read(stream, buf.data(), buf.size());
            if (nr > 0)
            {
                if (self->stream_handler_)
                {
                    self->exec_.spawn(self->stream_handler_(std::span<char>(buf.data(), nr)));
                }
            }
            else if (nr == 0)
            {
                ::lsquic_stream_close(stream);
            }
        }

        static void on_write(lsquic_stream_t* stream, lsquic_stream_ctx_t* /*ctx*/)
        {
            ::lsquic_stream_wantwrite(stream, 0);
        }

        static struct ssl_ctx_st* get_ssl_ctx(void* peer_ctx, const struct sockaddr* /*local*/)
        {
            auto* self = static_cast<impl*>(peer_ctx);
            return reinterpret_cast<struct ssl_ctx_st*>(self->ssl_ctx_);
        }

        static void on_close(lsquic_stream_t* /*stream*/, lsquic_stream_ctx_t* /*ctx*/)
        {
        }
    };

    engine::engine(executor& exec) noexcept : impl_(std::make_unique<impl>(exec))
    {
    }

    engine::~engine() noexcept = default;

    void engine::set_stream_handler(stream_handler_t handler) noexcept
    {
        impl_->stream_handler_ = std::move(handler);
    }

    task<std::expected<void, std::error_code>> engine::start(const ip_address_t ip, const port_t port, void* ssl_ctx, const kmx::aio::quic::settings& config) noexcept(false)
    {
        impl_->ssl_ctx_ = ssl_ctx;
        if (0 != ::lsquic_global_init(LSQUIC_GLOBAL_SERVER))
        {
            co_return std::unexpected(error_from_errno(EINVAL));
        }

        auto sock_addr_result = make_socket_address(ip, port);
        if (!sock_addr_result)
        {
            co_return std::unexpected(sock_addr_result.error());
        }

        // Using std::shared_ptr for executor based on typical completion APIs (requires enable_shared_from_this)
        // We retrieve the shared_ptr from exec_.
        auto sock_res = udp::socket::create(impl_->exec_.shared_from_this(), ip_family(ip));
        if (!sock_res)
        {
            co_return std::unexpected(sock_res.error());
        }

        impl_->socket_ = std::make_unique<udp::socket>(std::move(*sock_res));

        impl_->local_addr_ = sock_addr_result->storage;
        if (::bind(impl_->socket_->get_fd(), reinterpret_cast<sockaddr*>(&sock_addr_result->storage), sock_addr_result->length) < 0)
        {
            co_return std::unexpected(error_from_errno());
        }

        static struct lsquic_stream_if stream_if {};
        stream_if.on_new_conn = impl::on_new_conn;
        stream_if.on_conn_closed = impl::on_conn_closed;
        stream_if.on_new_stream = impl::on_new_stream;
        stream_if.on_read = impl::on_read;
        stream_if.on_write = impl::on_write;
        stream_if.on_close = impl::on_close;

        lsquic_engine_api engine_api {};
        engine_api.ea_packets_out = impl::send_packets_out;
        engine_api.ea_packets_out_ctx = impl_.get();
        engine_api.ea_stream_if = &stream_if;
        engine_api.ea_stream_if_ctx = impl_.get();
        engine_api.ea_get_ssl_ctx = impl::get_ssl_ctx;

        static lsquic_engine_settings lsquic_settings {};
        ::lsquic_engine_init_settings(&lsquic_settings, LSENG_SERVER);
        lsquic_settings.es_max_streams_in = config.max_streams_in;
        lsquic_settings.es_idle_timeout = config.idle_conn_timeout_sec;
        lsquic_settings.es_max_cfcw = config.max_cfcwnd;
        engine_api.ea_settings = &lsquic_settings;

        impl_->lsquic_engine_ = ::lsquic_engine_new(LSENG_SERVER, &engine_api);
        if (!impl_->lsquic_engine_)
        {
            co_return std::unexpected(error_from_errno(EINVAL));
        }

        co_return std::expected<void, std::error_code>{};
    }

    task<std::expected<void, std::error_code>> engine::process() noexcept(false)
    {
        impl_->running_ = true;
        std::array<std::byte, 4096> packet_buf {};

        while (impl_->running_)
        {
            ::lsquic_engine_process_conns(impl_->lsquic_engine_);

            struct msghdr msg {};
            struct iovec iov[1];
            sockaddr_storage peer_addr {};

            iov[0].iov_base = packet_buf.data();
            iov[0].iov_len = packet_buf.size();
            msg.msg_name = &peer_addr;
            msg.msg_namelen = sizeof(peer_addr);
            msg.msg_iov = iov;
            msg.msg_iovlen = 1;

            auto recv_res = co_await impl_->socket_->recvmsg(&msg);
            if (recv_res && *recv_res > 0)
            {
                int diff = ::lsquic_engine_packet_in(impl_->lsquic_engine_,
                                                 reinterpret_cast<const unsigned char*>(packet_buf.data()),
                                                 *recv_res,
                                                 reinterpret_cast<sockaddr*>(&impl_->local_addr_),
                                                 reinterpret_cast<sockaddr*>(&peer_addr),
                                                 reinterpret_cast<void*>(impl_.get()),
                                                 0);
                (void)diff;
            }
            else if (!recv_res)
            {
                co_return std::unexpected(recv_res.error());
            }
        }
        co_return std::expected<void, std::error_code>{};
    }
} // namespace kmx::aio::completion::quic

#endif // KMX_AIO_FEATURE_QUIC

