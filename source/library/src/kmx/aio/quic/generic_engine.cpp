/// @file src/kmx/aio/quic/generic_engine.cpp
/// @brief Consolidated QUIC engine implementation using explicit template instantiation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#if defined(KMX_AIO_FEATURE_QUIC)
    #include <kmx/aio/quic/generic_engine.hpp>
    #ifndef PCH
        #include <kmx/aio/quic/engine_impl.hpp>
        #include <kmx/aio/readiness/executor.hpp>
        #include <kmx/aio/readiness/udp/socket.hpp>
    #endif

namespace kmx::aio::quic
{
    // Explicit instantiation
    template class generic_engine<kmx::aio::readiness::executor, kmx::aio::readiness::udp::socket>;
}

#endif // KMX_AIO_FEATURE_QUIC
