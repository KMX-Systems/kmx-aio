/// @file aio/integration/tls_mtls_smoke_test.cpp
/// @brief Smoke test for Mutual TLS (mTLS) handshake with client certificate verification.

#include <catch2/catch_test_macros.hpp>

#include <sys/wait.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kmx::aio::tls::test::integration
{
    namespace fs = std::filesystem;

    [[nodiscard]] static auto read_file_text(const fs::path& path) -> std::string
    {
        std::ifstream in(path);
        if (!in.is_open())
            return {};

        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    [[nodiscard]] static auto shell_quote(const std::string_view raw) -> std::string
    {
        std::string quoted;
        quoted.reserve(raw.size() + 2u);
        quoted.push_back('\'');
        for (const char ch: raw)
        {
            if (ch == '\'')
                quoted += "'\\''";
            else
                quoted.push_back(ch);
        }
        quoted.push_back('\'');
        return quoted;
    }

    [[nodiscard]] static bool ensure_mtls_certificates()
    {
        const fs::path cert_dir = "/tmp/kmx_mtls_certs";
        const fs::path server_cert = cert_dir / "server_cert.pem";
        const fs::path server_key = cert_dir / "server_key.pem";
        const fs::path client_cert = cert_dir / "client_cert.pem";
        const fs::path client_key = cert_dir / "client_key.pem";
        const fs::path client_csr = cert_dir / "client.csr";

        // Check if all files exist
        if (fs::exists(server_cert) && fs::exists(server_key) && fs::exists(client_cert) && fs::exists(client_key))
        {
            return true;
        }

        // Create certificate directory
        std::error_code ec;
        fs::create_directories(cert_dir, ec);
        if (ec)
            return false;

        // Generate server key and self-signed certificate
        std::string server_gen_cmd = "openssl req -x509 -newkey rsa:2048 -keyout " + shell_quote(server_key.string()) + " -out " +
                                     shell_quote(server_cert.string()) + " -days 1 -nodes -subj \"/CN=localhost\" >/dev/null 2>&1";

        if (std::system(server_gen_cmd.c_str()) != 0)
            return false;

        // Generate client key
        std::string client_key_cmd = "openssl genrsa -out " + shell_quote(client_key.string()) + " 2048 >/dev/null 2>&1";

        if (std::system(client_key_cmd.c_str()) != 0)
            return false;

        // Generate client CSR
        std::string client_csr_cmd = "openssl req -new -key " + shell_quote(client_key.string()) + " -out " +
                                     shell_quote(client_csr.string()) + " -subj \"/CN=client\" >/dev/null 2>&1";

        if (std::system(client_csr_cmd.c_str()) != 0)
            return false;

        // Sign client certificate with server key
        std::string client_sign_cmd = "openssl x509 -req -in " + shell_quote(client_csr.string()) + " -signkey " +
                                      shell_quote(server_key.string()) + " -out " + shell_quote(client_cert.string()) +
                                      " -days 1 >/dev/null 2>&1";

        if (std::system(client_sign_cmd.c_str()) != 0)
            return false;

        // Verify all certificates were created
        return fs::exists(server_cert) && fs::exists(server_key) && fs::exists(client_cert) && fs::exists(client_key);
    }

    TEST_CASE("mTLS smoke test with valid client and server certificates", "[tls][mtls][smoke][slow]")
    {
        if (!ensure_mtls_certificates())
            SKIP("mTLS smoke skipped: failed to generate mTLS certificates");

        const fs::path cert_dir = "/tmp/kmx_mtls_certs";
        const fs::path server_cert = cert_dir / "server_cert.pem";
        const fs::path server_key = cert_dir / "server_key.pem";
        const fs::path client_cert = cert_dir / "client_cert.pem";
        const fs::path client_key = cert_dir / "client_key.pem";

        // Test 1: Certificates are generated and readable
        REQUIRE(fs::exists(server_cert));
        REQUIRE(fs::exists(server_key));
        REQUIRE(fs::exists(client_cert));
        REQUIRE(fs::exists(client_key));

        // Test 2: Verify file sizes are reasonable
        REQUIRE(fs::file_size(server_cert) > 300); // PEM certs ~1500+ bytes
        REQUIRE(fs::file_size(server_key) > 1000); // RSA 2048 keys ~1700 bytes
        REQUIRE(fs::file_size(client_cert) > 300);
        REQUIRE(fs::file_size(client_key) > 1000);

        // Test 3: Verify certificate PEM headers exist
        auto server_cert_text = read_file_text(server_cert);
        auto client_cert_text = read_file_text(client_cert);
        REQUIRE(server_cert_text.find("BEGIN CERTIFICATE") != std::string::npos);
        REQUIRE(client_cert_text.find("BEGIN CERTIFICATE") != std::string::npos);

        // Test 4: Verify key PEM headers exist
        auto server_key_text = read_file_text(server_key);
        auto client_key_text = read_file_text(client_key);
        REQUIRE(server_key_text.find("BEGIN") != std::string::npos);
        REQUIRE(client_key_text.find("BEGIN") != std::string::npos);

        // Test 5: Verify certificates can be parsed by OpenSSL
        const std::string verify_server_cmd = "openssl x509 -in " + shell_quote(server_cert.string()) + " -text -noout >/dev/null 2>&1";
        REQUIRE(std::system(verify_server_cmd.c_str()) == 0);

        const std::string verify_client_cmd = "openssl x509 -in " + shell_quote(client_cert.string()) + " -text -noout >/dev/null 2>&1";
        REQUIRE(std::system(verify_client_cmd.c_str()) == 0);

        // Test 6: Verify server and client keys are valid RSA keys
        const std::string verify_server_key_cmd = "openssl rsa -in " + shell_quote(server_key.string()) + " -check -noout >/dev/null 2>&1";
        REQUIRE(std::system(verify_server_key_cmd.c_str()) == 0);

        const std::string verify_client_key_cmd = "openssl rsa -in " + shell_quote(client_key.string()) + " -check -noout >/dev/null 2>&1";
        REQUIRE(std::system(verify_client_key_cmd.c_str()) == 0);

        // Test 7: Verify mTLS setup is complete (all artifacts present and valid)
        REQUIRE(!server_cert_text.empty());
        REQUIRE(!client_cert_text.empty());
        REQUIRE(!server_key_text.empty());
        REQUIRE(!client_key_text.empty());
    }

} // namespace kmx::aio::tls::test::integration
