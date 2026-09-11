/// @file inc/kmx/aio/benchmark/detail/socket_pair.hpp
/// @brief An anonymous socket pair for the raw-syscall reference benchmarks, closed on destruction.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <sys/socket.h>
#endif

namespace kmx::aio::benchmark::detail
{
    /// @brief An anonymous socket pair, closed on destruction.
    struct socket_pair
    {
        int fd[2] {-1, -1};

        explicit socket_pair(const int flags) noexcept { valid = ::socketpair(AF_UNIX, SOCK_STREAM | flags, 0, fd) == 0; }

        ~socket_pair() noexcept;

        socket_pair(const socket_pair&) = delete;
        socket_pair& operator=(const socket_pair&) = delete;

        bool valid {};
    };
}
