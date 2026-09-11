/// @file inc/kmx/aio/tls/detail/native_openssl_syscalls.hpp
/// @brief The far side of the OpenSSL seam: the real OpenSSL entry points, declared without OpenSSL's headers.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// `native_openssl_syscalls` carries the real calls and is defined in
/// src/kmx/aio/tls/detail/native_openssl_syscalls.cpp, which is the only place <openssl/bio.h> is included.
#pragma once

// OpenSSL and BoringSSL both spell these as typedefs of an incomplete struct, and both agree on the
// tag names, so repeating the typedef here is the whole of what the seam needs to know about either.
// A redeclaration identical to the one in <openssl/types.h> is well-formed, so a file that includes
// both this header and OpenSSL's - tls/stream.hpp does - still compiles.
/// @brief OpenSSL/BoringSSL BIO handle, redeclared so this header need not include OpenSSL.
typedef struct bio_st BIO; // NOLINT(modernize-use-using)
/// @brief OpenSSL/BoringSSL BIO method table, redeclared to keep OpenSSL out of this header.
typedef struct bio_method_st BIO_METHOD; // NOLINT(modernize-use-using)

namespace kmx::aio::tls::detail
{
    /// @brief The OpenSSL entry points the TLS stream needs to be able to fail.
    /// @note Defined in native_openssl_syscalls.cpp.
    struct native_openssl_syscalls
    {
        /// @brief Forwards to ::BIO_new.
        [[nodiscard]] static ::BIO* bio_new(const ::BIO_METHOD* method) noexcept;
    };
}
