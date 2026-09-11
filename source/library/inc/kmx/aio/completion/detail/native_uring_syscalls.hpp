/// @file inc/kmx/aio/completion/detail/native_uring_syscalls.hpp
/// @brief The far side of the io_uring seam: the real liburing calls, declared without <liburing.h>.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// `native_uring_syscalls` carries the real calls and is defined in
/// src/kmx/aio/completion/detail/native_uring_syscalls.cpp, so <liburing.h> stays in that one translation
/// unit.
///
/// The three liburing types that appear in a signature are forward declared. Nothing here needs their
/// layout, and the completion executor that passes them in has included <liburing.h> already.
#pragma once

struct io_uring;
struct io_uring_cqe;
struct __kernel_timespec; // NOLINT(bugprone-reserved-identifier): liburing's own name for the type.

namespace kmx::aio::completion::detail
{
    /// @brief The io_uring calls this library needs to be able to fail. Defined in native_uring_syscalls.cpp.
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
}
