/// @file inc/kmx/aio/benchmark/feature/detail/tls_contexts.hpp
/// @brief The TLS contexts a benchmark session needs, and the self-signed certificate they are configured with.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/test/scoped_ssl_ctx.hpp>
    #include <kmx/aio/test/tls_certs.hpp>

    #include <openssl/ssl.h>

    #include <filesystem>
    #include <string>
    #include <system_error>
#endif

namespace kmx::aio::benchmark::feature::detail
{
    /// @brief A self-signed certificate and its key, generated once for the whole run.
    /// @details Generating one is not what is being measured, and doing it per handshake would put
    ///          an openssl(1) fork into the middle of a benchmark. Reuses whatever is already on
    ///          disk from an earlier run.
    struct tls_credentials
    {
        std::string certificate; ///< Path to the certificate.
        std::string key;         ///< Path to the private key.
        bool usable {};          ///< False when openssl(1) could not produce them.
    };

    /// @brief Generates the run's certificate and key, reusing whatever an earlier run left on disk.
    /// @return The credentials, with usable false when they could not be made.
    [[nodiscard]] inline tls_credentials make_credentials() noexcept
    {
        const std::filesystem::path directory {"/tmp/kmx_aio_benchmark_certs"};
        const auto certificate = directory / "server_cert.pem";
        const auto key = directory / "server_key.pem";

        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (ec)
            return tls_credentials {certificate.string(), key.string(), false};

        const auto made = test::ensure_self_signed_pair(certificate, key, "localhost");
        return tls_credentials {certificate.string(), key.string(), made};
    }

    /// @brief Returns the run's credentials, generating them on first use.
    /// @return The credentials, with usable false when they could not be made.
    [[nodiscard]] inline const tls_credentials& shared_credentials() noexcept
    {
        static const tls_credentials credentials = make_credentials();
        return credentials;
    }

    /// @brief The two contexts a session needs, configured once per case.
    struct tls_contexts
    {
        test::scoped_ssl_ctx server {::TLS_server_method()}; ///< The accepting side's context.
        test::scoped_ssl_ctx client {::TLS_client_method()}; ///< The connecting side's context.

        /// @brief Loads the run's certificate into the server context and disables client verification.
        /// @return True when both contexts are usable.
        [[nodiscard]] bool configure() noexcept
        {
            const auto& credentials = shared_credentials();
            if (!credentials.usable || (server.get() == nullptr) || (client.get() == nullptr))
                return false;

            if (::SSL_CTX_use_certificate_chain_file(server.get(), credentials.certificate.c_str()) != 1)
                return false;

            if (::SSL_CTX_use_PrivateKey_file(server.get(), credentials.key.c_str(), SSL_FILETYPE_PEM) != 1)
                return false;

            // The certificate is self-signed and the point of the case is the handshake's cost, not
            // whether a chain validates. Verifying it would measure a trust store that no two
            // machines running this have configured the same way.
            ::SSL_CTX_set_verify(client.get(), SSL_VERIFY_NONE, nullptr);
            return true;
        }
    };
}
