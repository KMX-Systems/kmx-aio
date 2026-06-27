/// @file aio/readiness/tls/stream.hpp
/// @brief Readiness-model TLS stream using BoringSSL Memory BIOs over epoll.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/readiness/tcp/stream.hpp>
    #include <kmx/aio/tls/stream.hpp>
#endif

namespace kmx::aio::readiness::tls
{
    /// @brief Asynchronous TLS stream for the readiness (epoll) model.
    using stream = kmx::aio::tls::stream<kmx::aio::readiness::tcp::stream>;

} // namespace kmx::aio::readiness::tls
