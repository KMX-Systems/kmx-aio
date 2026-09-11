/// @file inc/kmx/aio/quic/engine_impl.hpp
/// @brief Private generic QUIC engine template definitions shared by model-specific instantiation units.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/quic/base_engine.hpp>
    #include <kmx/aio/quic/base_impl.hpp>
    #include <kmx/aio/quic/engine.hpp>
    #include <kmx/aio/quic/generic_engine.hpp>
    #include <kmx/aio/quic/settings.hpp>
    #include <kmx/aio/task.hpp>

    #include <cstddef>
    #include <memory>
    #include <string>
    #include <utility>
#endif

namespace kmx::aio::quic
{
    /// @brief The generic engine's hidden implementation: @ref base_impl with nothing added.
    /// @tparam Executor  The executor type (readiness::executor or completion::executor).
    /// @tparam UdpSocket The UDP socket type matching @p Executor.
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
    void generic_engine<Executor, UdpSocket>::set_alpn(std::string alpn) noexcept
    {
        impl_->alpn_ = std::move(alpn);
    }

    template <typename Executor, typename UdpSocket>
    void generic_engine<Executor, UdpSocket>::set_post_handshake_stream_count(const std::size_t count) noexcept
    {
        impl_->post_handshake_stream_count_ = count;
    }

    template <typename Executor, typename UdpSocket>
    void generic_engine<Executor, UdpSocket>::set_post_handshake_stream_writer(post_handshake_stream_writer_t writer) noexcept
    {
        impl_->post_handshake_stream_writer_ = std::move(writer);
    }

    template <typename Executor, typename UdpSocket>
    task_returning_expected_void_t generic_engine<Executor, UdpSocket>::start(const ip_address_t ip, const port_t port, void* ssl_ctx,
                                                                              const settings& config) noexcept(false)
    {
        const start_params params {.ip = ip, .port = port, .ssl_ctx = ssl_ctx, .config = config};
        if constexpr (requires { UdpSocket::create(impl_->exec_, ip_family(ip)); })
            co_return impl_->setup(UdpSocket::create(impl_->exec_, ip_family(ip)), params);
        else
            co_return impl_->setup(UdpSocket::create(impl_->exec_.shared_from_this(), ip_family(ip)), params);
    }

    template <typename Executor, typename UdpSocket>
    task_returning_expected_void_t generic_engine<Executor, UdpSocket>::connect(const connect_params params) noexcept(false)
    {
        if constexpr (requires { UdpSocket::create(impl_->exec_, ip_family(params.peer_ip)); })
            co_return impl_->connect_setup(UdpSocket::create(impl_->exec_, ip_family(params.peer_ip)), params);
        else
            co_return impl_->connect_setup(UdpSocket::create(impl_->exec_.shared_from_this(), ip_family(params.peer_ip)), params);
    }

    template <typename Executor, typename UdpSocket>
    task_returning_expected_void_t generic_engine<Executor, UdpSocket>::process() noexcept(false)
    {
        co_return co_await impl_->process();
    }
}
