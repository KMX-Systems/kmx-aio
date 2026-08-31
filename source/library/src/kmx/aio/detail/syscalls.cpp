/// @file aio/detail/syscalls.cpp
/// @brief The far side of the system-call seam: the only translation unit that calls the kernel.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// Everything here is a one-line forward. The point is not what these bodies do but where they are:
/// <sys/epoll.h>, <fcntl.h> and <sys/socket.h> are included below and nowhere near the seam's header,
/// so a file that wants to make a call fail includes a declaration and not the kernel's headers.
#include <kmx/aio/detail/syscalls.hpp>

#ifndef PCH
    #include <fcntl.h>
    #include <pthread.h>
    #include <sched.h>
    #include <sys/epoll.h>
    #include <sys/socket.h>
#endif

namespace kmx::aio::detail
{
    int native_syscalls::epoll_create1(const int flags) noexcept
    {
        return ::epoll_create1(flags);
    }

    int native_syscalls::epoll_wait(const int epfd, ::epoll_event* const events, const int max_events, const int timeout_ms) noexcept
    {
        return ::epoll_wait(epfd, events, max_events, timeout_ms);
    }

    int native_syscalls::fcntl(const int fd, const int cmd, const int arg) noexcept
    {
        return ::fcntl(fd, cmd, arg);
    }

    int native_syscalls::socket(const int domain, const int type, const int protocol) noexcept
    {
        return ::socket(domain, type, protocol);
    }

    int native_syscalls::pthread_setaffinity_np(const ::pthread_t thread, const std::size_t size, const ::cpu_set_t* const set) noexcept
    {
        return ::pthread_setaffinity_np(thread, size, set);
    }

    int native_syscalls::pthread_getaffinity_np(const ::pthread_t thread, const std::size_t size, ::cpu_set_t* const set) noexcept
    {
        return ::pthread_getaffinity_np(thread, size, set);
    }

} // namespace kmx::aio::detail
