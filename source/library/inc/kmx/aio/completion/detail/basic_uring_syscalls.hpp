/// @file inc/kmx/aio/completion/detail/basic_uring_syscalls.hpp
/// @brief The io_uring seam in front of the real calls: a forwarding specialization and a fault-injecting one.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// `basic_uring_syscalls` stands in front of native_uring_syscalls as two specializations: a production
/// one that only forwards, and a testing one, compiled under KMX_AIO_FAULT_INJECTION alone, that consults
/// the registry first.
///
/// The io_uring entry points do not use errno. They return 0 or a positive count on success and
/// -errno on failure, so an injected fault is returned negated rather than stored in the global.
#pragma once
#ifndef PCH
    #include <kmx/aio/completion/detail/native_uring_syscalls.hpp>
    #include <kmx/aio/detail/fault_registry.hpp>
#endif

namespace kmx::aio::completion::detail
{
    using aio::detail::syscall_id;

    /// @brief The seam in front of native_uring_syscalls. Only the two specializations below exist.
    template <bool injects_faults>
    struct basic_uring_syscalls;

    /// @brief The production seam: each call is nothing but a forward to native_uring_syscalls.
    template <>
    struct basic_uring_syscalls<false>
    {
        /// @brief False: this specialization carries no fault-injection code.
        static constexpr bool injects_faults {};

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
}
