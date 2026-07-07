/// @file aio/quic/engine_impl.hpp
/// @brief Private generic QUIC engine template definitions shared by model-specific instantiation units.
#pragma once

#include "kmx/aio/quic/base_engine.hpp"

namespace kmx::aio::quic
{
    template <typename Executor, typename UdpSocket>
    struct generic_engine<Executor, UdpSocket>::impl: base_impl<Executor, UdpSocket>
    {
        using base_impl<Executor, UdpSocket>::base_impl;
    };

    template <typename Executor, typename UdpSocket>
    generic_engine<Executor, UdpSocket>::generic_engine(Executor& exec) noexcept: impl_(std::make_unique<impl>(exec))
    {
    }

    template <typename Executor, typename UdpSocket>
    generic_engine<Executor, UdpSocket>::~generic_engine() noexcept = default;

    template <typename Executor, typename UdpSocket>
    void generic_engine<Executor, UdpSocket>::set_stream_handler(stream_handler_t handler) noexcept
    {
        impl_->stream_handler_ = std::move(handler);
    }

    template <typename Executor, typename UdpSocket>
    task<std::expected<void, std::error_code>> generic_engine<Executor, UdpSocket>::start(const ip_address_t ip, const port_t port,
                                                                                          void* ssl_ctx,
                                                                                          const settings& config) noexcept(false)
    {
        if constexpr (requires { UdpSocket::create(impl_->exec_, ip_family(ip)); })
            co_return impl_->setup(UdpSocket::create(impl_->exec_, ip_family(ip)), ip, port, ssl_ctx, config);
        else
            co_return impl_->setup(UdpSocket::create(impl_->exec_.shared_from_this(), ip_family(ip)), ip, port, ssl_ctx, config);
    }

    template <typename Executor, typename UdpSocket>
    task<std::expected<void, std::error_code>> generic_engine<Executor, UdpSocket>::connect(const ip_address_t peer_ip,
                                                                                            const port_t peer_port,
                                                                                            const std::string& hostname,
                                                                                            const std::string& payload, void* ssl_ctx,
                                                                                            const settings& config) noexcept(false)
    {
        if constexpr (requires { UdpSocket::create(impl_->exec_, ip_family(peer_ip)); })
            co_return impl_->connect_setup(UdpSocket::create(impl_->exec_, ip_family(peer_ip)), peer_ip, peer_port, hostname, payload,
                                           ssl_ctx, config);
        else
            co_return impl_->connect_setup(UdpSocket::create(impl_->exec_.shared_from_this(), ip_family(peer_ip)), peer_ip, peer_port,
                                           hostname, payload, ssl_ctx, config);
    }

    template <typename Executor, typename UdpSocket>
    task<std::expected<void, std::error_code>> generic_engine<Executor, UdpSocket>::connect(
        const ip_address_t peer_ip, const port_t peer_port, const std::string& hostname, const std::vector<std::string>& payloads,
        void* ssl_ctx, const settings& config) noexcept(false)
    {
        if constexpr (requires { UdpSocket::create(impl_->exec_, ip_family(peer_ip)); })
            co_return impl_->connect_setup(UdpSocket::create(impl_->exec_, ip_family(peer_ip)), peer_ip, peer_port, hostname, payloads,
                                           ssl_ctx, config);
        else
            co_return impl_->connect_setup(UdpSocket::create(impl_->exec_.shared_from_this(), ip_family(peer_ip)), peer_ip, peer_port,
                                           hostname, payloads, ssl_ctx, config);
    }

    template <typename Executor, typename UdpSocket>
    task<std::expected<void, std::error_code>> generic_engine<Executor, UdpSocket>::process() noexcept(false)
    {
        co_return co_await impl_->process();
    }
} // namespace kmx::aio::quic