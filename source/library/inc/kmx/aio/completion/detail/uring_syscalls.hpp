/// @file aio/completion/detail/uring_syscalls.hpp
/// @brief The io_uring half of the syscall seam, kept apart so that only liburing users pay for it.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// Split from aio/detail/syscalls.hpp because liburing is a dependency of the completion backend
/// alone: a readiness-only build should not have to find it in order to include the seam. The shape
/// is the one the core seam sets out - `native_uring_syscalls` carries the real calls and is defined
/// in src/kmx/aio/completion/detail/uring_syscalls.cpp, so <liburing.h> stays in that one translation
/// unit, and `basic_uring_syscalls` stands in front of it as two specializations: a production one
/// that only forwards, and a testing one, compiled under KMX_AIO_FAULT_INJECTION alone, that consults
/// the registry first.
///
/// The three liburing types that appear in a signature are forward declared. Nothing here needs their
/// layout, and the completion executor that passes them in has included <liburing.h> already.
///
/// The io_uring entry points do not use errno. They return 0 or a positive count on success and
/// -errno on failure, so an injected fault is returned negated rather than stored in the global.
#pragma once
#ifndef PCH
    #include <kmx/aio/detail/syscalls.hpp>
#endif

struct io_uring;
struct io_uring_cqe;
struct __kernel_timespec; // NOLINT(bugprone-reserved-identifier): liburing's own name for the type.

namespace kmx::aio::completion::detail
{
    using aio::detail::syscall_id;

    /// @brief The io_uring calls this library needs to be able to fail. Defined in uring_syscalls.cpp.
    struct native_uring_syscalls
    {
        /// @brief Forwards to ::io_uring_queue_init.
        [[nodiscard]] static int queue_init(unsigned entries, ::io_uring* ring, unsigned flags) noexcept;

        /// @brief Forwards to ::io_uring_submit.
        [[nodiscard]] static int submit(::io_uring* ring) noexcept;

        /// @brief Forwards to ::io_uring_wait_cqe_timeout.
        [[nodiscard]] static int wait_cqe_timeout(::io_uring* ring, ::io_uring_cqe** cqe, ::__kernel_timespec* ts) noexcept;

        /// @brief Forwards to ::io_uring_submit_and_wait_timeout.
        [[nodiscard]] static int submit_and_wait_timeout(::io_uring* ring, ::io_uring_cqe** cqe, unsigned wait_nr,
                                                         ::__kernel_timespec* ts) noexcept;
    };

    /// @brief The seam in front of native_uring_syscalls. Only the two specializations below exist.
    template <bool injects_faults>
    struct basic_uring_syscalls;

    /// @brief The production seam: each call is nothing but a forward to native_uring_syscalls.
    template <>
    struct basic_uring_syscalls<false>
    {
        /// @brief False: this specialization carries no fault-injection code.
        static constexpr bool injects_faults = false;

        /// @brief Wrapper for ::io_uring_queue_init.
        [[nodiscard]] static int queue_init(const unsigned entries, ::io_uring* const ring, const unsigned flags) noexcept
        {
            return native_uring_syscalls::queue_init(entries, ring, flags);
        }

        /// @brief Wrapper for ::io_uring_submit.
        [[nodiscard]] static int submit(::io_uring* const ring) noexcept { return native_uring_syscalls::submit(ring); }

        /// @brief Wrapper for ::io_uring_wait_cqe_timeout.
        [[nodiscard]] static int wait_cqe_timeout(::io_uring* const ring, ::io_uring_cqe** const cqe,
                                                  ::__kernel_timespec* const ts) noexcept
        {
            return native_uring_syscalls::wait_cqe_timeout(ring, cqe, ts);
        }

        /// @brief Wrapper for ::io_uring_submit_and_wait_timeout.
        [[nodiscard]] static int submit_and_wait_timeout(::io_uring* const ring, ::io_uring_cqe** const cqe, const unsigned wait_nr,
                                                         ::__kernel_timespec* const ts) noexcept
        {
            return native_uring_syscalls::submit_and_wait_timeout(ring, cqe, wait_nr, ts);
        }
    };

#if defined(KMX_AIO_FAULT_INJECTION)
    /// @brief The testing seam: each call asks the registry for a failure before forwarding.
    template <>
    struct basic_uring_syscalls<true>
    {
        /// @brief True: this specialization carries the fault-injection stubs.
        static constexpr bool injects_faults = true;

        /// @brief Stub for ::io_uring_queue_init.
        [[nodiscard]] static int queue_init(const unsigned entries, ::io_uring* const ring, const unsigned flags) noexcept
        {
            if (const int error = aio::detail::fault_registry::take(syscall_id::io_uring_queue_init); error != 0)
                return -error;

            return native_uring_syscalls::queue_init(entries, ring, flags);
        }

        /// @brief Stub for ::io_uring_submit.
        [[nodiscard]] static int submit(::io_uring* const ring) noexcept
        {
            if (const int error = aio::detail::fault_registry::take(syscall_id::io_uring_submit); error != 0)
                return -error;

            return native_uring_syscalls::submit(ring);
        }

        /// @brief Stub for ::io_uring_wait_cqe_timeout.
        [[nodiscard]] static int wait_cqe_timeout(::io_uring* const ring, ::io_uring_cqe** const cqe,
                                                  ::__kernel_timespec* const ts) noexcept
        {
            if (const int error = aio::detail::fault_registry::take(syscall_id::io_uring_wait_cqe_timeout); error != 0)
                return -error;

            return native_uring_syscalls::wait_cqe_timeout(ring, cqe, ts);
        }

        /// @brief Stub for ::io_uring_submit_and_wait_timeout.
        [[nodiscard]] static int submit_and_wait_timeout(::io_uring* const ring, ::io_uring_cqe** const cqe, const unsigned wait_nr,
                                                         ::__kernel_timespec* const ts) noexcept
        {
            if (const int error = aio::detail::fault_registry::take(syscall_id::io_uring_submit_and_wait_timeout); error != 0)
                return -error;

            return native_uring_syscalls::submit_and_wait_timeout(ring, cqe, wait_nr, ts);
        }
    };
#endif

#if defined(KMX_AIO_FAULT_INJECTION)
    /// @brief The seam the completion executor calls through, in a fault-injection build.
    using uring_syscalls = basic_uring_syscalls<true>;
#else
    /// @brief The seam the completion executor calls through. Nothing but a call to liburing is left.
    using uring_syscalls = basic_uring_syscalls<false>;
    static_assert(!uring_syscalls::injects_faults, "the production seam must carry no fault-injection code");
#endif

} // namespace kmx::aio::completion::detail
