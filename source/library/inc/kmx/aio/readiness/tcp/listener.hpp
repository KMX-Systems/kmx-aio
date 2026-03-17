/// @file aio/readiness/tcp/listener.hpp
/// @brief Readiness-model TCP listener using epoll-based async accept.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/tcp/listener.hpp>
#endif

namespace kmx::aio::readiness::tcp
{
    /// @brief The readiness-model TCP listener is the existing epoll-based listener.
    /// @details Uses epoll to detect when accept() will not block, then performs
    ///          the actual accept() system call in the coroutine.
    using listener = kmx::aio::tcp::listener;

} // namespace kmx::aio::readiness::tcp
