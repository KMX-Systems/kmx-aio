/// @file aio/readiness/quic/engine.cpp
/// @brief Readiness-model QUIC engine implementation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#if defined(KMX_AIO_FEATURE_QUIC)

#ifndef PCH
    #include <kmx/aio/readiness/quic/engine.hpp>
    #include <kmx/aio/readiness/udp/socket.hpp>
#endif

#include "kmx/aio/quic/base_engine.hpp"

namespace kmx::aio::readiness::quic
{
    /// @brief Concrete impl inherits all lsquic state from base_impl.
    struct engine::impl : kmx::aio::quic::base_impl<executor, udp::socket>
    {
        using kmx::aio::quic::base_impl<executor, udp::socket>::base_impl;
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

        // Create socket via readiness model (reference executor).
        auto sock_res = udp::socket::create(impl_->exec_, ip_family(ip));
        if (!sock_res)
            co_return std::unexpected(sock_res.error());

        impl_->socket_ = std::make_unique<udp::socket>(std::move(*sock_res));

        // Bind and initialise lsquic (shared logic).
        if (auto bind_res = impl_->bind_socket(ip, port); !bind_res)
            co_return std::unexpected(bind_res.error());

        if (auto init_res = impl_->init_lsquic(config); !init_res)
            co_return std::unexpected(init_res.error());

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
                co_return std::unexpected(recv_res.error());
        }

        co_return std::expected<void, std::error_code>{};
    }
} // namespace kmx::aio::readiness::quic

#endif // KMX_AIO_FEATURE_QUIC
