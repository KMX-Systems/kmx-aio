/// @file aio/tls/detail/tls_syscalls.hpp
/// @brief The OpenSSL half of the fault-injection seam.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// Split from aio/detail/syscalls.hpp for the same reason the io_uring half is: OpenSSL is a
/// dependency of the TLS layer alone, and the seam should not drag it into every translation unit that
/// wants to make a system call fail. `native_tls_syscalls` carries the real calls and is defined in
/// src/kmx/aio/tls/detail/tls_syscalls.cpp, which is the only place <openssl/bio.h> is included;
/// `basic_tls_syscalls` stands in front of it as two specializations, a forwarding one and a testing
/// one that is compiled under KMX_AIO_FAULT_INJECTION alone.
///
/// Only the allocations the TLS stream constructor has to recover from are wrapped. A BIO that cannot
/// be created is not a theoretical concern - it is the shape every allocation failure takes during
/// session setup, and the constructor's job is to release what it already owns rather than leak an SSL
/// and half a BIO pair on the way out.
#pragma once
#ifndef PCH
    #include <kmx/aio/detail/syscalls.hpp>
#endif

// OpenSSL and BoringSSL both spell these as typedefs of an incomplete struct, and both agree on the
// tag names, so repeating the typedef here is the whole of what the seam needs to know about either.
// A redeclaration identical to the one in <openssl/types.h> is well-formed, so a file that includes
// both this header and OpenSSL's - tls/stream.hpp does - still compiles.
typedef struct bio_st BIO;               // NOLINT(modernize-use-using)
typedef struct bio_method_st BIO_METHOD; // NOLINT(modernize-use-using)

namespace kmx::aio::tls::detail
{
    using aio::detail::syscall_id;

    /// @brief The OpenSSL entry points the TLS stream needs to be able to fail.
    /// @note Defined in tls_syscalls.cpp.
    struct native_tls_syscalls
    {
        /// @brief Forwards to ::BIO_new.
        [[nodiscard]] static ::BIO* bio_new(const ::BIO_METHOD* method) noexcept;
    };

    /// @brief The seam in front of native_tls_syscalls. Only the two specializations below exist.
    template <bool injects_faults>
    struct basic_tls_syscalls;

    /// @brief The production seam: the call is nothing but a forward to native_tls_syscalls.
    template <>
    struct basic_tls_syscalls<false>
    {
        /// @brief False: this specialization carries no fault-injection code.
        static constexpr bool injects_faults = false;

        /// @brief Wrapper for ::BIO_new.
        [[nodiscard]] static ::BIO* bio_new(const ::BIO_METHOD* const method) noexcept { return native_tls_syscalls::bio_new(method); }
    };

#if defined(KMX_AIO_FAULT_INJECTION)
    /// @brief The testing seam: the call asks the registry for a failure before forwarding.
    template <>
    struct basic_tls_syscalls<true>
    {
        /// @brief True: this specialization carries the fault-injection stub.
        static constexpr bool injects_faults = true;

        /// @brief Stub for ::BIO_new.
        /// @note OpenSSL reports failure as a null pointer and does not use errno, so an injected fault
        ///       is turned into the null the caller already knows how to handle.
        [[nodiscard]] static ::BIO* bio_new(const ::BIO_METHOD* const method) noexcept
        {
            if (aio::detail::fault_registry::take(syscall_id::bio_new) != 0)
                return nullptr;

            return native_tls_syscalls::bio_new(method);
        }
    };
#endif

#if defined(KMX_AIO_FAULT_INJECTION)
    /// @brief The seam the TLS stream calls through, in a fault-injection build.
    using tls_syscalls = basic_tls_syscalls<true>;
#else
    /// @brief The seam the TLS stream calls through. Nothing but a call to OpenSSL is left.
    using tls_syscalls = basic_tls_syscalls<false>;
    static_assert(!tls_syscalls::injects_faults, "the production seam must carry no fault-injection code");
#endif

} // namespace kmx::aio::tls::detail
