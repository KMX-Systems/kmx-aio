/// @file aio/quic/transport.cpp
/// @brief ALPN wiring for the QUIC transport.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#if defined(KMX_AIO_FEATURE_QUIC)

    #include <cstring>
    #include <kmx/aio/quic/transport.hpp>
    #include <openssl/ssl.h>

namespace kmx::aio::quic
{
    namespace detail
    {
        /// @brief The name a server accepts; one per process, which is all this layer needs.
        static std::string server_alpn;

        /// @brief Selects the configured name out of the client's offer.
        static int select_alpn(::SSL*, const unsigned char** out, unsigned char* out_len, const unsigned char* in,
                               unsigned int in_len, void*) noexcept
        {
            // The offer is a sequence of length-prefixed names; walk it looking for the one we speak.
            for (unsigned int i = 0u; i < in_len;)
            {
                const unsigned int length = in[i];
                if ((length == 0u) || ((i + 1u + length) > in_len))
                    break;

                if ((length == server_alpn.size()) && (std::memcmp(&in[i + 1u], server_alpn.data(), length) == 0))
                {
                    *out = &in[i + 1u];
                    *out_len = static_cast<unsigned char>(length);
                    return SSL_TLSEXT_ERR_OK;
                }

                i += 1u + length;
            }

            return SSL_TLSEXT_ERR_ALERT_FATAL;
        }
    }

    void configure_server_alpn(void* const ssl_ctx, const char* const alpn) noexcept
    {
        if ((ssl_ctx == nullptr) || (alpn == nullptr))
            return;

        detail::server_alpn = alpn;
        ::SSL_CTX_set_alpn_select_cb(static_cast<::SSL_CTX*>(ssl_ctx), &detail::select_alpn, nullptr);
    }
}

#endif // KMX_AIO_FEATURE_QUIC
