/// @file inc/kmx/aio/test/scoped_ssl_ctx.hpp
/// @brief SSL_CTX ownership for the TLS, mTLS and QUIC tests.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <openssl/ssl.h>
#endif

namespace kmx::aio::test
{
    /// @brief An SSL_CTX released on destruction.
    /// @details OpenSSL hands back a raw owning pointer, and a test that returns early - through a
    ///          failed REQUIRE, say - would otherwise leak it. Under a leak sanitizer that turns an
    ///          already-failing test into two failures pointing at different things.
    class scoped_ssl_ctx
    {
    public:
        /// @brief Creates a context for @p method.
        /// @param method The OpenSSL method; TLS_method() by default, which negotiates either role.
        explicit scoped_ssl_ctx(const ::SSL_METHOD* const method = ::TLS_method()) noexcept: ctx_(::SSL_CTX_new(method)) {}

        /// @brief Adopts an already-created context.
        /// @param ctx The context to take ownership of; may be nullptr.
        /// @param adopt Tag distinguishing this from the creating constructor.
        struct adopt_t
        {
        };
        static constexpr adopt_t adopt {}; ///< Tag selecting the adopting constructor.

        /// @brief Takes ownership of @p ctx.
        /// @param ctx The context to own; may be nullptr.
        scoped_ssl_ctx(::SSL_CTX* const ctx, adopt_t) noexcept: ctx_(ctx) {}

        scoped_ssl_ctx(const scoped_ssl_ctx&) = delete;
        scoped_ssl_ctx& operator=(const scoped_ssl_ctx&) = delete;

        /// @brief Frees the context if there is one.
        ~scoped_ssl_ctx() noexcept
        {
            if (ctx_ != nullptr)
                ::SSL_CTX_free(ctx_);
        }

        /// @brief The owned context.
        /// @return The context, or nullptr when creation failed.
        [[nodiscard]] ::SSL_CTX* get() const noexcept { return ctx_; }

    private:
        ::SSL_CTX* ctx_ {};
    };
}
