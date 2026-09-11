/// @file src/kmx/aio/completion/quic/engine.cpp
/// @brief Completion-model explicit QUIC engine instantiation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details kmx::aio::completion::quic::engine is an alias of the generic engine, so the instantiation it needs
///          names a template owned by kmx::aio::quic. The language only accepts an explicit instantiation from a
///          namespace enclosing that template, which kmx::aio::completion::quic is not; it is therefore written
///          at global scope, fully qualified, rather than by reopening kmx::aio::quic from this directory.
#if defined(KMX_AIO_FEATURE_QUIC)
    #include <kmx/aio/completion/quic/engine.hpp>
    #ifndef PCH
        #include <kmx/aio/completion/executor.hpp>
        #include <kmx/aio/completion/udp/socket.hpp>
        #include <kmx/aio/quic/engine_impl.hpp>
    #endif

template class kmx::aio::quic::generic_engine<kmx::aio::completion::executor, kmx::aio::completion::udp::socket>;

#endif // KMX_AIO_FEATURE_QUIC
