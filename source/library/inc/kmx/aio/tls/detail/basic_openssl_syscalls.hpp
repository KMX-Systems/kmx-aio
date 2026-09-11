/// @file inc/kmx/aio/tls/detail/basic_openssl_syscalls.hpp
/// @brief The OpenSSL seam in front of the real calls: a forwarding specialization and a fault-injecting one.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// `basic_openssl_syscalls` stands in front of native_openssl_syscalls as two specializations, a forwarding
/// one and a testing one that is compiled under KMX_AIO_FAULT_INJECTION alone.
#pragma once
#ifndef PCH
    #include <kmx/aio/detail/fault_registry.hpp>
    #include <kmx/aio/tls/detail/native_openssl_syscalls.hpp>
#endif

namespace kmx::aio::tls::detail
{
    using aio::detail::syscall_id;

    /// @brief The seam in front of native_openssl_syscalls. Only the two specializations below exist.
    template <bool injects_faults>
    struct basic_openssl_syscalls;

    /// @brief The production seam: the call is nothing but a forward to native_openssl_syscalls.
    template <>
    struct basic_openssl_syscalls<false>
    {
        /// @brief False: this specialization carries no fault-injection code.
        static constexpr bool injects_faults {};

        /// @brief Wrapper for ::BIO_new.
        [[nodiscard]] static ::BIO* bio_new(const ::BIO_METHOD* const method) noexcept { return native_openssl_syscalls::bio_new(method); }
    };

#if defined(KMX_AIO_FAULT_INJECTION)
    /// @brief The testing seam: the call asks the registry for a failure before forwarding.
    template <>
    struct basic_openssl_syscalls<true>
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

            return native_openssl_syscalls::bio_new(method);
        }
    };
#endif
}
