/// @file aio/readiness/executor.hpp
/// @brief Readiness-model executor using epoll for event notification.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/executor.hpp>
#endif

namespace kmx::aio::readiness
{
    /// @brief The readiness-model executor is the existing epoll-based executor.
    /// @details In the readiness model, the kernel signals that a file descriptor
    ///          is ready for I/O, and the application then performs the actual
    ///          read/write system call. This is the standard epoll workflow.
    using executor = kmx::aio::executor;

    /// @brief Configuration for the readiness executor.
    using executor_config = kmx::aio::executor_config;

} // namespace kmx::aio::readiness
