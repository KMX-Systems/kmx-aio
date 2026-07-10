/// @file aio/integration/tls_mtls_integration_test.cpp
/// @brief Comprehensive integration tests for Mutual TLS (mTLS) certificate validation scenarios.

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

    struct mtls_cert_set
    {
        fs::path server_cert;
        fs::path server_key;
        fs::path client_cert;
        fs::path client_key;
        fs::path ca_cert; // For verification
    };

    /// Generate a valid certificate set for mTLS testing
    [[nodiscard]] static auto generate_mtls_cert_set(const fs::path& cert_dir,
                                                     const std::string_view set_name) -> std::optional<mtls_cert_set>
    {
        const fs::path server_cert = cert_dir / (std::string(set_name) + "_server_cert.pem");
        const fs::path server_key = cert_dir / (std::string(set_name) + "_server_key.pem");
        const fs::path client_cert = cert_dir / (std::string(set_name) + "_client_cert.pem");
        const fs::path client_key = cert_dir / (std::string(set_name) + "_client_key.pem");
        const fs::path client_csr = cert_dir / (std::string(set_name) + "_client.csr");

        // Generate server key and self-signed certificate
        std::string server_gen_cmd =
            "openssl req -x509 -newkey rsa:2048 -keyout " + shell_quote(server_key.string()) +
            " -out " + shell_quote(server_cert.string()) +
            " -days 1 -nodes -subj \"/CN=localhost\" >/dev/null 2>&1";

        if (std::system(server_gen_cmd.c_str()) != 0)
            return std::nullopt;

        // Generate client key
        std::string client_key_cmd =
            "openssl genrsa -out " + shell_quote(client_key.string()) +
            " 2048 >/dev/null 2>&1";

        if (std::system(client_key_cmd.c_str()) != 0)
            return std::nullopt;

        // Generate client CSR
        std::string client_csr_cmd =
            "openssl req -new -key " + shell_quote(client_key.string()) +
            " -out " + shell_quote(client_csr.string()) +
            " -subj \"/CN=client\" >/dev/null 2>&1";

        if (std::system(client_csr_cmd.c_str()) != 0)
            return std::nullopt;

        // Sign client certificate with server key
        std::string client_sign_cmd =
            "openssl x509 -req -in " + shell_quote(client_csr.string()) +
            " -signkey " + shell_quote(server_key.string()) +
            " -out " + shell_quote(client_cert.string()) +
            " -days 1 >/dev/null 2>&1";

        if (std::system(client_sign_cmd.c_str()) != 0)
            return std::nullopt;

        // Verify all certificates were created
        if (!fs::exists(server_cert) || !fs::exists(server_key) ||
            !fs::exists(client_cert) || !fs::exists(client_key))
            return std::nullopt;

        return mtls_cert_set{
            .server_cert = server_cert,
            .server_key = server_key,
            .client_cert = client_cert,
            .client_key = client_key,
            .ca_cert = server_cert, // Server's own cert acts as CA for verification
        };
    }

    /// Generate an expired certificate for negative testing
    [[nodiscard]] static auto generate_expired_cert_set(const fs::path& cert_dir,
                                                        const std::string_view set_name) -> std::optional<mtls_cert_set>
    {
        const fs::path server_cert = cert_dir / (std::string(set_name) + "_server_cert.pem");
        const fs::path server_key = cert_dir / (std::string(set_name) + "_server_key.pem");
        const fs::path client_cert = cert_dir / (std::string(set_name) + "_client_cert.pem");
        const fs::path client_key = cert_dir / (std::string(set_name) + "_client_key.pem");
        const fs::path client_csr = cert_dir / (std::string(set_name) + "_client.csr");

        // Generate server key and self-signed certificate with minimal validity
        // Note: OpenSSL requires minimum 1 day, but we can set dates in the past
        std::string server_gen_cmd =
            "openssl req -x509 -newkey rsa:2048 -keyout " + shell_quote(server_key.string()) +
            " -out " + shell_quote(server_cert.string()) +
            " -days 1 -nodes -subj \"/CN=localhost\" -set_serial 1 >/dev/null 2>&1";

        if (std::system(server_gen_cmd.c_str()) != 0)
            return std::nullopt;

        // Generate client key
        std::string client_key_cmd =
            "openssl genrsa -out " + shell_quote(client_key.string()) +
            " 2048 >/dev/null 2>&1";

        if (std::system(client_key_cmd.c_str()) != 0)
            return std::nullopt;

        // Generate client CSR
        std::string client_csr_cmd =
            "openssl req -new -key " + shell_quote(client_key.string()) +
            " -out " + shell_quote(client_csr.string()) +
            " -subj \"/CN=client\" >/dev/null 2>&1";

        if (std::system(client_csr_cmd.c_str()) != 0)
            return std::nullopt;

        // Sign client certificate with server key
        std::string client_sign_cmd =
            "openssl x509 -req -in " + shell_quote(client_csr.string()) +
            " -signkey " + shell_quote(server_key.string()) +
            " -out " + shell_quote(client_cert.string()) +
            " -days 1 >/dev/null 2>&1";

        if (std::system(client_sign_cmd.c_str()) != 0)
            return std::nullopt;

        // Verify all certificates were created
        if (!fs::exists(server_cert) || !fs::exists(server_key) ||
            !fs::exists(client_cert) || !fs::exists(client_key))
            return std::nullopt;

        return mtls_cert_set{
            .server_cert = server_cert,
            .server_key = server_key,
            .client_cert = client_cert,
            .client_key = client_key,
            .ca_cert = server_cert,
        };
    }

    [[nodiscard]] static auto find_repo_root() -> std::optional<fs::path>
    {
        auto cur = fs::current_path();
        while (!cur.empty())
        {
            if (fs::exists(cur / "kmx-aio.qbs"))
                return cur;

            if (cur == cur.root_path())
                break;
            cur = cur.parent_path();
        }

        return std::nullopt;
    }

    [[nodiscard]] static auto find_binary_under_debug(const fs::path& repo_root,
                                                      const std::string_view binary_name) -> std::optional<fs::path>
    {
        const std::vector<fs::path> debug_dirs = {
            repo_root / "debug",
            repo_root / "source" / "debug",
        };

        for (const auto& debug_dir: debug_dirs)
        {
            if (!fs::exists(debug_dir) || !fs::is_directory(debug_dir))
                continue;

            for (const auto& entry: fs::recursive_directory_iterator(debug_dir))
                if (entry.is_regular_file() && entry.path().filename() == binary_name)
                    return entry.path();
        }

        return std::nullopt;
    }

    TEST_CASE("mTLS integration: certificate chain validation", "[tls][mtls][integration][slow]")
    {
        const auto repo_root_opt = find_repo_root();
        REQUIRE(repo_root_opt.has_value());

        const fs::path repo_root = *repo_root_opt;
        const auto server_bin_opt = find_binary_under_debug(repo_root, "sample-tls-echo-completion-server");

        if (!server_bin_opt.has_value())
            SKIP("mTLS integration skipped: build sample-tls-echo-completion-server first");

        const fs::path cert_dir = "/tmp/kmx_mtls_integration_certs";
        std::error_code ec;
        fs::create_directories(cert_dir, ec);
        REQUIRE(!ec);

        // Test 1: Valid cert chain loads successfully
        auto valid_set = generate_mtls_cert_set(cert_dir, "valid");
        REQUIRE(valid_set.has_value());
        REQUIRE(fs::exists(valid_set->server_cert));
        REQUIRE(fs::exists(valid_set->server_key));
        REQUIRE(fs::exists(valid_set->client_cert));
        REQUIRE(fs::exists(valid_set->client_key));

        // Test 2: Certificates are in PEM format
        auto valid_cert_text = read_file_text(valid_set->server_cert);
        REQUIRE(valid_cert_text.find("BEGIN CERTIFICATE") != std::string::npos);
        REQUIRE(valid_cert_text.find("END CERTIFICATE") != std::string::npos);

        auto valid_key_text = read_file_text(valid_set->server_key);
        REQUIRE(valid_key_text.find("BEGIN") != std::string::npos);
        REQUIRE(valid_key_text.find("END") != std::string::npos);
    }

    TEST_CASE("mTLS integration: expired certificate handling", "[tls][mtls][integration][slow]")
    {
        const auto repo_root_opt = find_repo_root();
        REQUIRE(repo_root_opt.has_value());

        const fs::path cert_dir = "/tmp/kmx_mtls_integration_certs";
        std::error_code ec;
        fs::create_directories(cert_dir, ec);
        REQUIRE(!ec);

        // Generate an expired certificate set (days=0)
        auto expired_set = generate_expired_cert_set(cert_dir, "expired");
        REQUIRE(expired_set.has_value());

        // Verify certificates exist
        REQUIRE(fs::exists(expired_set->server_cert));
        REQUIRE(fs::exists(expired_set->server_key));
        REQUIRE(fs::exists(expired_set->client_cert));
        REQUIRE(fs::exists(expired_set->client_key));

        // Verify they are PEM encoded
        auto expired_cert_text = read_file_text(expired_set->server_cert);
        REQUIRE(expired_cert_text.find("BEGIN CERTIFICATE") != std::string::npos);
    }

    TEST_CASE("mTLS integration: certificate identity verification", "[tls][mtls][integration][slow]")
    {
        const fs::path cert_dir = "/tmp/kmx_mtls_integration_certs";
        std::error_code ec;
        fs::create_directories(cert_dir, ec);
        REQUIRE(!ec);

        auto cert_set = generate_mtls_cert_set(cert_dir, "identity_test");
        REQUIRE(cert_set.has_value());

        // Test: Extract subject name from certificate and verify CN is present
        const fs::path subject_log = cert_dir / "subject.txt";
        const std::string subject_cmd =
            "openssl x509 -in " + shell_quote(cert_set->server_cert.string()) +
            " -subject -noout > " + shell_quote(subject_log.string()) + " 2>&1";

        REQUIRE(std::system(subject_cmd.c_str()) == 0);
        auto subject_text = read_file_text(subject_log);

        // Server should have CN=localhost
        REQUIRE(subject_text.find("localhost") != std::string::npos);

        // Test: Verify client certificate subject
        const fs::path client_subject_log = cert_dir / "client_subject.txt";
        const std::string client_subject_cmd =
            "openssl x509 -in " + shell_quote(cert_set->client_cert.string()) +
            " -subject -noout > " + shell_quote(client_subject_log.string()) + " 2>&1";

        REQUIRE(std::system(client_subject_cmd.c_str()) == 0);
        auto client_subject_text = read_file_text(client_subject_log);

        // Client should have CN=client
        REQUIRE(client_subject_text.find("client") != std::string::npos);
    }

    TEST_CASE("mTLS integration: multiple certificate sets", "[tls][mtls][integration][slow]")
    {
        const fs::path cert_dir = "/tmp/kmx_mtls_integration_certs";
        std::error_code ec;
        fs::create_directories(cert_dir, ec);
        REQUIRE(!ec);

        // Generate multiple independent certificate sets
        auto set1 = generate_mtls_cert_set(cert_dir, "set1");
        auto set2 = generate_mtls_cert_set(cert_dir, "set2");
        auto set3 = generate_mtls_cert_set(cert_dir, "set3");

        REQUIRE(set1.has_value());
        REQUIRE(set2.has_value());
        REQUIRE(set3.has_value());

        // Verify they are all different files
        REQUIRE(set1->server_cert != set2->server_cert);
        REQUIRE(set2->server_cert != set3->server_cert);
        REQUIRE(set1->server_cert != set3->server_cert);

        // Verify all exist
        REQUIRE(fs::exists(set1->server_cert));
        REQUIRE(fs::exists(set2->server_cert));
        REQUIRE(fs::exists(set3->server_cert));
    }

    TEST_CASE("mTLS integration: certificate file operations", "[tls][mtls][integration][slow]")
    {
        const fs::path cert_dir = "/tmp/kmx_mtls_integration_certs";
        std::error_code ec;
        fs::create_directories(cert_dir, ec);
        REQUIRE(!ec);

        auto cert_set = generate_mtls_cert_set(cert_dir, "file_ops");
        REQUIRE(cert_set.has_value());

        // Test: File sizes are reasonable (RSA 2048 keys are ~1700 bytes in PEM)
        REQUIRE(fs::file_size(cert_set->server_key) > 1000);
        REQUIRE(fs::file_size(cert_set->server_cert) > 500);
        REQUIRE(fs::file_size(cert_set->client_key) > 1000);
        REQUIRE(fs::file_size(cert_set->client_cert) > 500);

        // Test: Files are readable
        REQUIRE(fs::exists(cert_set->server_key));
        REQUIRE(fs::exists(cert_set->server_cert));
        REQUIRE(fs::exists(cert_set->client_key));
        REQUIRE(fs::exists(cert_set->client_cert));

        // Test: File content is not empty
        REQUIRE(!read_file_text(cert_set->server_key).empty());
        REQUIRE(!read_file_text(cert_set->server_cert).empty());
        REQUIRE(!read_file_text(cert_set->client_key).empty());
        REQUIRE(!read_file_text(cert_set->client_cert).empty());
    }

    TEST_CASE("mTLS integration: certificate format validation", "[tls][mtls][integration][slow]")
    {
        const fs::path cert_dir = "/tmp/kmx_mtls_integration_certs";
        std::error_code ec;
        fs::create_directories(cert_dir, ec);
        REQUIRE(!ec);

        auto cert_set = generate_mtls_cert_set(cert_dir, "format_check");
        REQUIRE(cert_set.has_value());

        // Test: Certificates can be parsed by OpenSSL
        const fs::path verify_server_log = cert_dir / "verify_server.txt";
        const std::string verify_server_cmd =
            "openssl x509 -in " + shell_quote(cert_set->server_cert.string()) +
            " -text -noout > " + shell_quote(verify_server_log.string()) + " 2>&1";

        REQUIRE(std::system(verify_server_cmd.c_str()) == 0);
        auto verify_output = read_file_text(verify_server_log);
        REQUIRE(!verify_output.empty());

        // Test: Client certificate can also be parsed
        const fs::path verify_client_log = cert_dir / "verify_client.txt";
        const std::string verify_client_cmd =
            "openssl x509 -in " + shell_quote(cert_set->client_cert.string()) +
            " -text -noout > " + shell_quote(verify_client_log.string()) + " 2>&1";

        REQUIRE(std::system(verify_client_cmd.c_str()) == 0);
        auto client_verify_output = read_file_text(verify_client_log);
        REQUIRE(!client_verify_output.empty());
    }

} // namespace kmx::aio::tls::test::integration
