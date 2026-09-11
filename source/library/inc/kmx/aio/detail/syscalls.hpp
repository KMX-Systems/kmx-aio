/// @file inc/kmx/aio/detail/syscalls.hpp
/// @brief A two-part seam over the system calls whose failures the library reacts to.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// A handful of branches in this library exist only to handle a system call failing: epoll_wait
/// returning EINTR, io_uring_submit refusing a submission, a core pin being rejected. They are the
/// branches that matter most when they finally run, and the ones that never run in a test - a machine
/// does not fail its syscalls on request.
///
/// This seam makes them reachable, and it is split in two on purpose: `native_syscalls`
/// (native_syscalls.hpp) holds the real calls, and `basic_syscalls<injects_faults>` (basic_syscalls.hpp)
/// stands in front of it. The `syscalls` alias below picks between them, and
/// static_assert(!syscalls::injects_faults) states which one a production build got.
#pragma once
#ifndef PCH
    #include <kmx/aio/detail/basic_syscalls.hpp>
#endif

namespace kmx::aio::detail
{
#if defined(KMX_AIO_FAULT_INJECTION)
    /// @brief The seam the library calls through, in a fault-injection build.
    using syscalls = basic_syscalls<true>;
#else
    /// @brief The seam the library calls through. Nothing but a call to the kernel wrapper is left.
    using syscalls = basic_syscalls<false>;
    static_assert(!syscalls::injects_faults, "the production seam must carry no fault-injection code");
#endif

}
