/// @file aio/test/tls_certs.hpp
/// @brief Certificate material and SSL_CTX ownership for the TLS, mTLS and QUIC tests.
/// @details None of these tests are about generating certificates - they need one to exist so a
///          handshake can happen. Shelling out to openssl(1) is how the suite does that, and doing it
///          once here keeps the seven copies of the key/CSR/sign sequence from drifting apart.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdlib>
    #include <filesystem>
    #include <optional>
    #include <string>
    #include <string_view>

    #include <openssl/ssl.h>

    #include <kmx/aio/test/sample_process.hpp>
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

    /// @brief Paths to one generated mTLS certificate set.
    struct cert_set
    {
        std::filesystem::path ca_cert;     ///< The CA that signed both leaf certificates.
        std::filesystem::path ca_key;      ///< The CA's private key.
        std::filesystem::path server_cert; ///< The server's certificate, signed by @ref ca_cert.
        std::filesystem::path server_key;  ///< The server's private key.
        std::filesystem::path client_cert; ///< The client's certificate, signed by @ref ca_cert.
        std::filesystem::path client_key;  ///< The client's private key.
    };

    namespace detail
    {
        /// @brief Runs one openssl(1) invocation, discarding its output.
        /// @param command The command line, with every path already quoted.
        /// @return True when openssl exited successfully.
        [[nodiscard]] inline bool run_openssl(const std::string& command) noexcept(false)
        {
            return std::system((command + " >/dev/null 2>&1").c_str()) == 0;
        }
    } // namespace detail

    /// @brief Generates a self-signed certificate and its key, unless both already exist.
    /// @details For the tests that only need the handshake to complete - the peer is not verifying who
    ///          it is talking to, so one self-signed certificate is the whole requirement.
    /// @param cert_path Where to write the certificate.
    /// @param key_path Where to write the private key.
    /// @param common_name The subject CN; must match the host a verifying peer connects to.
    /// @return True when both files exist afterwards.
    [[nodiscard]] inline bool ensure_self_signed_pair(const std::filesystem::path& cert_path, const std::filesystem::path& key_path,
                                                      const std::string_view common_name = "localhost") noexcept(false)
    {
        namespace fs = std::filesystem;

        if (fs::exists(cert_path) && fs::exists(key_path))
            return true;

        const auto command = "openssl req -x509 -newkey rsa:2048 -keyout " + shell_quote(key_path.string()) + " -out " +
                             shell_quote(cert_path.string()) + " -days 1 -nodes -subj " + shell_quote("/CN=" + std::string(common_name));

        return detail::run_openssl(command) && fs::exists(cert_path) && fs::exists(key_path);
    }

    /// @brief Generates a CA plus a server and a client certificate it signed, under @p dir.
    /// @details This is what an mTLS test needs and a self-signed pair cannot give it: both peers verify
    ///          the other against the same CA, so a certificate that CA did not sign has to be rejected.
    ///          That rejection is the behaviour under test, and it cannot be exercised with certificates
    ///          that verify against nothing.
    /// @param dir The directory to write into; created if absent.
    /// @param server_common_name The server's CN; must match the address the client connects to.
    /// @param client_common_name The client's CN.
    /// @return The generated paths, or nothing when any openssl step failed.
    [[nodiscard]] inline std::optional<cert_set> ensure_ca_signed_set(
        const std::filesystem::path& dir, const std::string_view server_common_name = "127.0.0.1",
        const std::string_view client_common_name = "kmx-test-client") noexcept(false)
    {
        namespace fs = std::filesystem;

        const cert_set paths {
            .ca_cert = dir / "ca_cert.pem",
            .ca_key = dir / "ca_key.pem",
            .server_cert = dir / "server_cert.pem",
            .server_key = dir / "server_key.pem",
            .client_cert = dir / "client_cert.pem",
            .client_key = dir / "client_key.pem",
        };

        const auto server_csr = dir / "server.csr";
        const auto client_csr = dir / "client.csr";

        const auto looks_generated = [](const fs::path& cert)
        { return fs::exists(cert) && (read_file_text(cert).find("BEGIN CERTIFICATE") != std::string::npos); };

        // Reusing an existing set keeps a test that runs twice from paying for key generation twice; it
        // is checked for content rather than existence, so a truncated file from an interrupted run is
        // regenerated instead of being handed on as if it were valid.
        if (looks_generated(paths.ca_cert) && looks_generated(paths.server_cert) && looks_generated(paths.client_cert) &&
            fs::exists(paths.server_key) && fs::exists(paths.client_key))
            return paths;

        std::error_code ignored;
        fs::create_directories(dir, ignored);

        const auto q = [](const fs::path& p) { return shell_quote(p.string()); };

        const bool ok = detail::run_openssl("openssl req -x509 -newkey rsa:2048 -keyout " + q(paths.ca_key) + " -out " + q(paths.ca_cert) +
                                            " -days 30 -nodes -subj " + shell_quote("/CN=KmxAioTestCA")) &&
                        detail::run_openssl("openssl genrsa -out " + q(paths.server_key) + " 2048") &&
                        detail::run_openssl("openssl req -new -key " + q(paths.server_key) + " -out " + q(server_csr) + " -subj " +
                                            shell_quote("/CN=" + std::string(server_common_name))) &&
                        detail::run_openssl("openssl x509 -req -in " + q(server_csr) + " -CA " + q(paths.ca_cert) + " -CAkey " +
                                            q(paths.ca_key) + " -CAcreateserial -out " + q(paths.server_cert) + " -days 30") &&
                        detail::run_openssl("openssl genrsa -out " + q(paths.client_key) + " 2048") &&
                        detail::run_openssl("openssl req -new -key " + q(paths.client_key) + " -out " + q(client_csr) + " -subj " +
                                            shell_quote("/CN=" + std::string(client_common_name))) &&
                        detail::run_openssl("openssl x509 -req -in " + q(client_csr) + " -CA " + q(paths.ca_cert) + " -CAkey " +
                                            q(paths.ca_key) + " -CAcreateserial -out " + q(paths.client_cert) + " -days 30");

        if (!ok)
            return std::nullopt;

        return paths;
    }

    /// @brief Whether openssl(1) can parse @p path as a certificate.
    /// @param path The PEM file to check.
    /// @return True when it parses.
    [[nodiscard]] inline bool verify_certificate(const std::filesystem::path& path) noexcept(false)
    {
        return detail::run_openssl("openssl x509 -in " + shell_quote(path.string()) + " -text -noout");
    }

    /// @brief Whether openssl(1) accepts @p path as a consistent RSA private key.
    /// @param path The PEM file to check.
    /// @return True when it checks out.
    [[nodiscard]] inline bool verify_private_key(const std::filesystem::path& path) noexcept(false)
    {
        return detail::run_openssl("openssl rsa -in " + shell_quote(path.string()) + " -check -noout");
    }

} // namespace kmx::aio::test
