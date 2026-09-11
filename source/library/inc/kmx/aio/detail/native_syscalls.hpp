/// @file inc/kmx/aio/detail/native_syscalls.hpp
/// @brief The far side of the system-call seam: the real calls, declared without the kernel's headers.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// `native_syscalls` holds the real calls. It is *declared* here and *defined* in
/// src/kmx/aio/detail/native_syscalls.cpp, so the headers the calls need - <sys/epoll.h>, <fcntl.h>,
/// <sys/socket.h> - stay inside that one translation unit instead of reaching every file that
/// wants to make a call fail. Only the types that appear in a signature are named here, and
/// `epoll_event` is named by forward declaration alone.
#pragma once
#ifndef PCH
    #include <cstddef>

    // pthread_t and cpu_set_t are typedefs of opaque or anonymous types, so unlike epoll_event they
    // cannot be forward declared. These two are the only system headers the seam cannot shed.
    #include <pthread.h>
    #include <sched.h>
#endif

/// @brief Declared rather than included: only its address crosses the seam.
struct epoll_event;

namespace kmx::aio::detail
{
    /// @brief The system calls this library needs to be able to fail. Defined in native_syscalls.cpp.
    /// @note Nothing calls this directly: it is the far side of the seam, and the library goes through
    ///       `syscalls` so that a test can get in between.
    struct native_syscalls
    {
        /// @brief Forwards to ::epoll_create1.
        [[nodiscard]] static int epoll_create1(int flags) noexcept;

        /// @brief Forwards to ::epoll_wait.
        [[nodiscard]] static int epoll_wait(int epfd, ::epoll_event* events, int max_events, int timeout_ms) noexcept;

        /// @brief Forwards to ::fcntl.
        [[nodiscard]] static int fcntl(int fd, int cmd, int arg) noexcept;

        /// @brief Forwards to ::socket.
        [[nodiscard]] static int socket(int domain, int type, int protocol) noexcept;

        /// @brief Forwards to ::pthread_setaffinity_np.
        [[nodiscard]] static int pthread_setaffinity_np(::pthread_t thread, std::size_t size, const ::cpu_set_t* set) noexcept;

        /// @brief Forwards to ::pthread_getaffinity_np.
        [[nodiscard]] static int pthread_getaffinity_np(::pthread_t thread, std::size_t size, ::cpu_set_t* set) noexcept;
    };
}
