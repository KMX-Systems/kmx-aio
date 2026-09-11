/// @file inc/kmx/aio/completion/detail/uring_syscalls.hpp
/// @brief The io_uring half of the syscall seam, kept apart so that only liburing users pay for it.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// Split from aio/detail/syscalls.hpp because liburing is a dependency of the completion backend
/// alone: a readiness-only build should not have to find it in order to include the seam. The shape
/// is the one the core seam sets out - `native_uring_syscalls` (native_uring_syscalls.hpp) carries the
/// real calls, and `basic_uring_syscalls` (basic_uring_syscalls.hpp) stands in front of it.
#pragma once
#ifndef PCH
    #include <kmx/aio/completion/detail/basic_uring_syscalls.hpp>
#endif

namespace kmx::aio::completion::detail
{
#if defined(KMX_AIO_FAULT_INJECTION)
    /// @brief The seam the completion executor calls through, in a fault-injection build.
    using uring_syscalls = basic_uring_syscalls<true>;
#else
    /// @brief The seam the completion executor calls through. Nothing but a call to liburing is left.
    using uring_syscalls = basic_uring_syscalls<false>;
    static_assert(!uring_syscalls::injects_faults, "the production seam must carry no fault-injection code");
#endif

}
