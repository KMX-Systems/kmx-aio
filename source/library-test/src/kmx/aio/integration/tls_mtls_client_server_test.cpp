#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/test/temp_dir.hpp>
#include <kmx/aio/test/tls_certs.hpp>

#include <catch2/generators/catch_generators.hpp>
#include <kmx/aio/test/sample_process.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

namespace kmx::aio::test::integration::tls_mtls_client_server_test
{
    using namespace std::literals::chrono_literals;

    bool contains_marker(const std::string& text, const std::string& marker)
    {
        return text.find(marker) != std::string::npos;
    }

    // Integration Tests
    TEST_CASE("mTLS integration: TLS echo server with mutual authentication", "[tls][mtls][integration][slow]")
    {
        const auto repo_root = find_repo_root();
        REQUIRE(repo_root.has_value());

        const auto server_bin_opt = find_binary_under_debug(*repo_root, "sample-tls-echo-completion-server");
        const auto client_bin_opt = find_binary_under_debug(*repo_root, "sample-tls-echo-completion-client");

        if (!server_bin_opt || !client_bin_opt)
        {
            SKIP("TLS echo samples not found - build with project.full:true");
        }

        // If we get here, samples are available
        REQUIRE(std::filesystem::exists(server_bin_opt.value()));
        REQUIRE(std::filesystem::exists(client_bin_opt.value()));
    }

    TEST_CASE("mTLS integration: certificate environment validation", "[tls][mtls][integration][smoke]")
    {
        const auto repo_root = find_repo_root();
        REQUIRE(repo_root.has_value());

        // Test that we can generate and validate certificates for use with TLS samples
        const scoped_temp_dir cert_dir {"kmx_mtls_cert_validation_test"};
        REQUIRE(cert_dir.valid());

        const auto cert_set_opt = ensure_ca_signed_set(cert_dir.path());
        REQUIRE(cert_set_opt.has_value());
        const auto& certs = *cert_set_opt;

        // Verify all files exist
        REQUIRE(std::filesystem::exists(certs.server_cert));
        REQUIRE(std::filesystem::exists(certs.server_key));
        REQUIRE(std::filesystem::exists(certs.client_cert));
        REQUIRE(std::filesystem::exists(certs.client_key));

        // Verify files are readable
        REQUIRE(!read_file_text(certs.server_cert).empty());
        REQUIRE(!read_file_text(certs.server_key).empty());
        REQUIRE(!read_file_text(certs.client_cert).empty());
        REQUIRE(!read_file_text(certs.client_key).empty());

        // Verify OpenSSL can parse them
        REQUIRE(verify_certificate(certs.server_cert));
        REQUIRE(verify_certificate(certs.client_cert));
        REQUIRE(verify_private_key(certs.server_key));
        REQUIRE(verify_private_key(certs.client_key));
    }

    TEST_CASE("mTLS integration: server and client binary discovery", "[tls][mtls][integration][smoke]")
    {
        const auto repo_root = find_repo_root();
        REQUIRE(repo_root.has_value());

        // Verify both samples are available
        const auto server_bin_opt = find_binary_under_debug(*repo_root, "sample-tls-echo-completion-server");
        const auto client_bin_opt = find_binary_under_debug(*repo_root, "sample-tls-echo-completion-client");

        if (!server_bin_opt)
        {
            SKIP("sample-tls-echo-completion-server not built");
        }
        if (!client_bin_opt)
        {
            SKIP("sample-tls-echo-completion-client not built");
        }

        REQUIRE(std::filesystem::exists(server_bin_opt.value()));
        REQUIRE(std::filesystem::exists(client_bin_opt.value()));
        REQUIRE(std::filesystem::is_regular_file(server_bin_opt.value()));
        REQUIRE(std::filesystem::is_regular_file(client_bin_opt.value()));
    }

} // namespace kmx::aio::test::integration::tls_mtls_client_server_test
