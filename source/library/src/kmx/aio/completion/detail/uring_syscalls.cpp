/// @file aio/completion/detail/uring_syscalls.cpp
/// @brief The far side of the io_uring seam: the only translation unit that includes <liburing.h>.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/completion/detail/uring_syscalls.hpp>

#ifndef PCH
    #include <liburing.h>
#endif

namespace kmx::aio::completion::detail
{
    int native_uring_syscalls::queue_init(const unsigned entries, ::io_uring* const ring, const unsigned flags) noexcept
    {
        return ::io_uring_queue_init(entries, ring, flags);
    }

    int native_uring_syscalls::submit(::io_uring* const ring) noexcept
    {
        return ::io_uring_submit(ring);
    }

    int native_uring_syscalls::wait_cqe_timeout(::io_uring* const ring, ::io_uring_cqe** const cqe, ::__kernel_timespec* const ts) noexcept
    {
        return ::io_uring_wait_cqe_timeout(ring, cqe, ts);
    }

    int native_uring_syscalls::submit_and_wait_timeout(::io_uring* const ring, ::io_uring_cqe** const cqe, const unsigned wait_nr,
                                                       ::__kernel_timespec* const ts) noexcept
    {
        return ::io_uring_submit_and_wait_timeout(ring, cqe, wait_nr, ts, nullptr);
    }

} // namespace kmx::aio::completion::detail
