/// @file aio/tls/detail/tls_syscalls.cpp
/// @brief The far side of the TLS seam: the only translation unit that includes <openssl/bio.h>.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/tls/detail/tls_syscalls.hpp>

#ifndef PCH
    #include <openssl/bio.h>
#endif

namespace kmx::aio::tls::detail
{
    ::BIO* native_tls_syscalls::bio_new(const ::BIO_METHOD* const method) noexcept
    {
        return ::BIO_new(method);
    }

} // namespace kmx::aio::tls::detail
