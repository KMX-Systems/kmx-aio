/// @file inc/kmx/aio/tls/detail/tls_syscalls.hpp
/// @brief The OpenSSL half of the fault-injection seam.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// Split from aio/detail/syscalls.hpp for the same reason the io_uring half is: OpenSSL is a
/// dependency of the TLS layer alone, and the seam should not drag it into every translation unit that
/// wants to make a system call fail. `native_openssl_syscalls` (native_openssl_syscalls.hpp) carries the
/// real calls, and `basic_openssl_syscalls` (basic_openssl_syscalls.hpp) stands in front of it.
///
/// Only the allocations the TLS stream constructor has to recover from are wrapped. A BIO that cannot
/// be created is not a theoretical concern - it is the shape every allocation failure takes during
/// session setup, and the constructor's job is to release what it already owns rather than leak an SSL
/// and half a BIO pair on the way out.
#pragma once
#ifndef PCH
    #include <kmx/aio/tls/detail/basic_openssl_syscalls.hpp>
#endif

namespace kmx::aio::tls::detail
{
#if defined(KMX_AIO_FAULT_INJECTION)
    /// @brief The seam the TLS stream calls through, in a fault-injection build.
    using openssl_syscalls = basic_openssl_syscalls<true>;
#else
    /// @brief The seam the TLS stream calls through. Nothing but a call to OpenSSL is left.
    using openssl_syscalls = basic_openssl_syscalls<false>;
    static_assert(!openssl_syscalls::injects_faults, "the production seam must carry no fault-injection code");
#endif

}
