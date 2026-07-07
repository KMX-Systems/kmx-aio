/// @file aio/completion/quic/engine.cpp
/// @brief Completion-model explicit QUIC engine instantiation.

#if defined(KMX_AIO_FEATURE_QUIC)

    #ifndef PCH
        #include <kmx/aio/completion/executor.hpp>
        #include <kmx/aio/completion/quic/engine.hpp>
        #include <kmx/aio/completion/udp/socket.hpp>
    #endif

    #include "kmx/aio/quic/engine_impl.hpp"

namespace kmx::aio::quic
{
    template class generic_engine<kmx::aio::completion::executor, kmx::aio::completion::udp::socket>;
}

#endif // KMX_AIO_FEATURE_QUIC