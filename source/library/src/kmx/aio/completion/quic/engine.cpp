/// @file aio/completion/quic/engine.cpp
/// @brief Completion-model QUIC engine implementation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#if defined(KMX_AIO_FEATURE_QUIC)

#ifndef PCH
    #include <kmx/aio/completion/quic/engine.hpp>
    #include <kmx/aio/completion/udp/socket.hpp>
#endif

#include "../../quic/base_engine.hpp"

namespace kmx::aio::completion::quic
{
    struct engine::impl : kmx::aio::quic::base_impl<completion::executor, udp::socket>
    {
        using kmx::aio::quic::base_impl<completion::executor, udp::socket>::base_impl;
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
        co_return impl_->setup(
            udp::socket::create(impl_->exec_.shared_from_this(), ip_family(ip)),
            ip, port, ssl_ctx, config);
    }

    task<std::expected<void, std::error_code>> engine::process() noexcept(false)
    {
        co_return co_await impl_->process();
    }
} // namespace kmx::aio::completion::quic

#endif // KMX_AIO_FEATURE_QUIC
