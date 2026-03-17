/// @file aio/readiness/tcp/stream.hpp
/// @brief Readiness-model TCP stream using epoll-based async I/O.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/tcp/stream.hpp>
#endif

namespace kmx::aio::readiness::tcp
{
    /// @brief The readiness-model TCP stream is the existing epoll-based stream.
    /// @details Satisfies the kmx::aio::stream concept. Wakes the coroutine when
    ///          the socket is ready, then the coroutine calls recv()/send().
    using stream = kmx::aio::tcp::stream;

} // namespace kmx::aio::readiness::tcp
