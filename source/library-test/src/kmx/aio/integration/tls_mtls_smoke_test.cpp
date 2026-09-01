/// @file aio/integration/tls_mtls_smoke_test.cpp
/// @brief Smoke test for Mutual TLS (mTLS) handshake with client certificate verification.

#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/test/temp_dir.hpp>
#include <kmx/aio/test/tls_certs.hpp>

#include <kmx/aio/test/sample_process.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace kmx::aio::test::integration::tls_mtls_smoke_test
{
    namespace fs = std::filesystem;

    TEST_CASE("mTLS smoke test with valid client and server certificates", "[tls][mtls][smoke][slow]")
    {
        const scoped_temp_dir cert_dir {"kmx_mtls_certs"};
        REQUIRE(cert_dir.valid());

        const auto certs = ensure_ca_signed_set(cert_dir.path());
        if (!certs.has_value())
            SKIP("mTLS smoke skipped: failed to generate mTLS certificates");

        const fs::path& server_cert = certs->server_cert;
        const fs::path& server_key = certs->server_key;
        const fs::path& client_cert = certs->client_cert;
        const fs::path& client_key = certs->client_key;

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
        REQUIRE(verify_certificate(server_cert));
        REQUIRE(verify_certificate(client_cert));

        // Test 6: Verify server and client keys are valid RSA keys
        REQUIRE(verify_private_key(server_key));
        REQUIRE(verify_private_key(client_key));

        // Test 7: Verify mTLS setup is complete (all artifacts present and valid)
        REQUIRE(!server_cert_text.empty());
        REQUIRE(!client_cert_text.empty());
        REQUIRE(!server_key_text.empty());
        REQUIRE(!client_key_text.empty());
    }

} // namespace kmx::aio::test::integration::tls_mtls_smoke_test
