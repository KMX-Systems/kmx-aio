/// @file aio/quic/engine.cpp
/// @brief Consolidated QUIC engine implementation using explicit template instantiation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#if defined(KMX_AIO_FEATURE_QUIC)

    #ifndef PCH
        #include <kmx/aio/quic/engine.hpp>
        #include <kmx/aio/readiness/executor.hpp>
        #include <kmx/aio/readiness/udp/socket.hpp>
    #endif

    #include "kmx/aio/quic/engine_impl.hpp"

namespace kmx::aio::quic
{
    // Explicit instantiation
    template class generic_engine<kmx::aio::readiness::executor, kmx::aio::readiness::udp::socket>;

} // namespace kmx::aio::quic

#endif // KMX_AIO_FEATURE_QUIC
