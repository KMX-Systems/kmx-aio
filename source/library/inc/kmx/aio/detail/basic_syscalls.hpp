/// @file inc/kmx/aio/detail/basic_syscalls.hpp
/// @brief The seam in front of the real system calls: a forwarding specialization and a fault-injecting one.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// `basic_syscalls<injects_faults>` stands in front of native_syscalls. It has no primary definition: the
/// two specializations below are the whole of it, and they share nothing but their signatures.
/// `basic_syscalls<false>` is a straight forward to native_syscalls with no fault-handling code in
/// it at all - not a discarded branch, not a folded one, none written. `basic_syscalls<true>`
/// consults the registry before each call, and is compiled only when KMX_AIO_FAULT_INJECTION is
/// defined, which kmx_instrumentation sets alongside the coverage flags.
///
/// Writing the two apart rather than as one body under `if constexpr` is what lets the production
/// specialization say plainly what it is.
#pragma once
#ifndef PCH
    #include <kmx/aio/detail/native_syscalls.hpp>

    #include <cstddef>
    #include <pthread.h>
    #include <sched.h>
#endif

#if defined(KMX_AIO_FAULT_INJECTION)
    #ifndef PCH
        #include <kmx/aio/detail/fault_registry.hpp>

        #include <cerrno>
    #endif
#endif

namespace kmx::aio::detail
{
    /// @brief The seam in front of native_syscalls. Only the two specializations below exist.
    template <bool injects_faults>
    struct basic_syscalls;

    /// @brief The production seam: each call is nothing but a forward to native_syscalls.
    template <>
    struct basic_syscalls<false>
    {
        /// @brief False: this specialization carries no fault-injection code.
        static constexpr bool injects_faults {};

        /// @brief Wrapper for ::epoll_create1.
        [[nodiscard]] static int epoll_create1(const int flags) noexcept { return native_syscalls::epoll_create1(flags); }

        /// @brief Wrapper for ::epoll_wait.
        [[nodiscard]] static int epoll_wait(const int epfd, ::epoll_event* const events, const int max_events,
                                            const int timeout_ms) noexcept
        {
            return native_syscalls::epoll_wait(epfd, events, max_events, timeout_ms);
        }

        /// @brief Wrapper for ::fcntl.
        [[nodiscard]] static int fcntl(const int fd, const int cmd, const int arg) noexcept { return native_syscalls::fcntl(fd, cmd, arg); }

        /// @brief Wrapper for ::socket.
        [[nodiscard]] static int socket(const int domain, const int type, const int protocol) noexcept
        {
            return native_syscalls::socket(domain, type, protocol);
        }

        /// @brief Wrapper for ::pthread_setaffinity_np.
        [[nodiscard]] static int pthread_setaffinity_np(const ::pthread_t thread, const std::size_t size,
                                                        const ::cpu_set_t* const set) noexcept
        {
            return native_syscalls::pthread_setaffinity_np(thread, size, set);
        }

        /// @brief Wrapper for ::pthread_getaffinity_np.
        [[nodiscard]] static int pthread_getaffinity_np(const ::pthread_t thread, const std::size_t size, ::cpu_set_t* const set) noexcept
        {
            return native_syscalls::pthread_getaffinity_np(thread, size, set);
        }
    };

#if defined(KMX_AIO_FAULT_INJECTION)
    /// @brief The testing seam: each call asks the registry for a failure before forwarding.
    /// @note Compiled only under KMX_AIO_FAULT_INJECTION, so a production build has no definition of
    ///       this specialization to instantiate even by mistake.
    template <>
    struct basic_syscalls<true>
    {
        /// @brief True: this specialization carries the fault-injection stubs.
        static constexpr bool injects_faults = true;

        /// @brief Stub for ::epoll_create1.
        [[nodiscard]] static int epoll_create1(const int flags) noexcept
        {
            if (const int error = fault_registry::take(syscall_id::epoll_create1); error != 0)
            {
                errno = error;
                return -1;
            }

            return native_syscalls::epoll_create1(flags);
        }

        /// @brief Stub for ::epoll_wait.
        [[nodiscard]] static int epoll_wait(const int epfd, ::epoll_event* const events, const int max_events,
                                            const int timeout_ms) noexcept
        {
            if (const int error = fault_registry::take(syscall_id::epoll_wait); error != 0)
            {
                errno = error;
                return -1;
            }

            return native_syscalls::epoll_wait(epfd, events, max_events, timeout_ms);
        }

        /// @brief Stub for ::fcntl.
        [[nodiscard]] static int fcntl(const int fd, const int cmd, const int arg) noexcept
        {
            if (const int error = fault_registry::take(syscall_id::fcntl); error != 0)
            {
                errno = error;
                return -1;
            }

            return native_syscalls::fcntl(fd, cmd, arg);
        }

        /// @brief Stub for ::socket.
        [[nodiscard]] static int socket(const int domain, const int type, const int protocol) noexcept
        {
            if (const int error = fault_registry::take(syscall_id::socket); error != 0)
            {
                errno = error;
                return -1;
            }

            return native_syscalls::socket(domain, type, protocol);
        }

        /// @brief Stub for ::pthread_setaffinity_np.
        /// @note The pthread calls report an errno as their return value and leave the global alone, so
        ///       an injected failure is returned rather than stored.
        [[nodiscard]] static int pthread_setaffinity_np(const ::pthread_t thread, const std::size_t size,
                                                        const ::cpu_set_t* const set) noexcept
        {
            if (const int error = fault_registry::take(syscall_id::pthread_setaffinity_np); error != 0)
                return error;

            return native_syscalls::pthread_setaffinity_np(thread, size, set);
        }

        /// @brief Stub for ::pthread_getaffinity_np.
        [[nodiscard]] static int pthread_getaffinity_np(const ::pthread_t thread, const std::size_t size, ::cpu_set_t* const set) noexcept
        {
            if (const int error = fault_registry::take(syscall_id::pthread_getaffinity_np); error != 0)
                return error;

            return native_syscalls::pthread_getaffinity_np(thread, size, set);
        }
    };
#endif
}
