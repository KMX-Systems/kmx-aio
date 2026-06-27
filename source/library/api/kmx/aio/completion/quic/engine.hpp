/// @file aio/completion/quic/engine.hpp
/// @brief Completion-model QUIC engine alias.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#if defined(KMX_AIO_FEATURE_QUIC)

    #ifndef PCH
        #include <kmx/aio/completion/executor.hpp>
        #include <kmx/aio/completion/udp/socket.hpp>
        #include <kmx/aio/quic/engine.hpp>
    #endif

namespace kmx::aio::completion::quic
{
    /// @brief QUIC engine for the completion (io_uring) model.
    /// @details Alias to the consolidated generic QUIC engine.
    using engine = kmx::aio::quic::generic_engine<executor, udp::socket>;

} // namespace kmx::aio::completion::quic

#endif // KMX_AIO_FEATURE_QUIC
