/// @file aio/completion/tls/stream.hpp
/// @brief Completion-model TLS stream using BoringSSL Memory BIOs over io_uring.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/completion/tcp/stream.hpp>
    #include <kmx/aio/tls/stream.hpp>
#endif

namespace kmx::aio::completion::tls
{
    /// @brief Asynchronous TLS stream for the completion (io_uring) model.
    using stream = kmx::aio::tls::stream<kmx::aio::completion::tcp::stream>;

} // namespace kmx::aio::completion::tls
