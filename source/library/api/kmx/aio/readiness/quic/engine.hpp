/// @file aio/readiness/quic/engine.hpp
/// @brief Readiness-model QUIC engine alias.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#if defined(KMX_AIO_FEATURE_QUIC)

    #ifndef PCH
        #include <kmx/aio/quic/engine.hpp>
        #include <kmx/aio/readiness/executor.hpp>
        #include <kmx/aio/readiness/udp/socket.hpp>
    #endif

namespace kmx::aio::readiness::quic
{
    /// @brief QUIC engine for the readiness (epoll) model.
    /// @details Alias to the consolidated generic QUIC engine.
    using engine = kmx::aio::quic::generic_engine<executor, udp::socket>;

} // namespace kmx::aio::readiness::quic

#endif // KMX_AIO_FEATURE_QUIC
