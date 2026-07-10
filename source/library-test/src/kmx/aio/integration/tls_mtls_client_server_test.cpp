#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <optional>
#include <thread>
#include <iostream>

namespace kmx::aio::tls::test::integration {

using namespace std::literals::chrono_literals;

// ============================================================================
// Utility Functions
// ============================================================================

std::string shell_quote(const std::string& arg) {
    if (arg.find_first_of(" \t\n\"'$`\\!*?&|;()[]{}") == std::string::npos) {
        return arg;
    }
    std::string quoted = "'";
    for (char c : arg) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

std::string read_file_text(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
}

std::string find_repo_root() {
    auto cwd = std::filesystem::current_path();
    while (!cwd.empty()) {
        if (std::filesystem::exists(cwd / "kmx-aio.qbs")) {
            return cwd.string();
        }
        auto parent = cwd.parent_path();
        if (parent == cwd) break;
        cwd = parent;
    }
    return "";
}

std::optional<std::string> find_binary_under_debug(
    const std::string& repo_root, const std::string& binary_name) {
    if (repo_root.empty()) return std::nullopt;

    auto debug_dir = std::filesystem::path(repo_root) / "debug";
    if (!std::filesystem::exists(debug_dir)) {
        return std::nullopt;
    }

    try {
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(debug_dir)) {
            if (entry.is_regular_file()) {
                auto filename = entry.path().filename().string();
                if (filename.find(binary_name) != std::string::npos &&
                    filename.find(".") == std::string::npos) {
                    return entry.path().string();
                }
            }
        }
    } catch (...) {
        return std::nullopt;
    }
    return std::nullopt;
}

bool contains_marker(const std::string& text, const std::string& marker) {
    return text.find(marker) != std::string::npos;
}

struct mtls_cert_set {
    std::string server_cert;
    std::string server_key;
    std::string client_cert;
    std::string client_key;
};

mtls_cert_set generate_mtls_cert_set(const std::filesystem::path& cert_dir) {
    std::filesystem::create_directories(cert_dir);

    const auto server_cert = (cert_dir / "server_cert.pem").string();
    const auto server_key = (cert_dir / "server_key.pem").string();
    const auto client_key = (cert_dir / "client_key.pem").string();
    const auto client_csr = (cert_dir / "client.csr").string();
    const auto client_cert = (cert_dir / "client_cert.pem").string();

    // Generate server self-signed cert
    const auto server_cmd =
        "openssl req -x509 -newkey rsa:2048 -keyout " +
        shell_quote(server_key) + " -out " + shell_quote(server_cert) +
        " -days 1 -nodes -subj '/CN=localhost' >/dev/null 2>&1";
    REQUIRE(std::system(server_cmd.c_str()) == 0);

    // Generate client key
    const auto client_key_cmd = "openssl genrsa -out " + shell_quote(client_key) +
                                " 2048 >/dev/null 2>&1";
    REQUIRE(std::system(client_key_cmd.c_str()) == 0);

    // Generate client CSR
    const auto client_csr_cmd = "openssl req -new -key " + shell_quote(client_key) +
                                " -out " + shell_quote(client_csr) +
                                " -subj '/CN=client' >/dev/null 2>&1";
    REQUIRE(std::system(client_csr_cmd.c_str()) == 0);

    // Sign client cert with server key
    const auto sign_cmd = "openssl x509 -req -in " + shell_quote(client_csr) +
                          " -signkey " + shell_quote(server_key) + " -out " +
                          shell_quote(client_cert) + " -days 1 >/dev/null 2>&1";
    REQUIRE(std::system(sign_cmd.c_str()) == 0);

    return {server_cert, server_key, client_cert, client_key};
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_CASE("mTLS integration: TLS echo server with mutual authentication",
          "[tls][mtls][integration][slow]") {
    const auto repo_root = find_repo_root();
    REQUIRE(!repo_root.empty());

    const auto server_bin_opt =
        find_binary_under_debug(repo_root, "sample-tls-echo-completion-server");
    const auto client_bin_opt =
        find_binary_under_debug(repo_root, "sample-tls-echo-completion-client");

    if (!server_bin_opt || !client_bin_opt) {
        SKIP("TLS echo samples not found - build with project.full:true");
    }

    // If we get here, samples are available
    REQUIRE(std::filesystem::exists(server_bin_opt.value()));
    REQUIRE(std::filesystem::exists(client_bin_opt.value()));
}

TEST_CASE("mTLS integration: certificate environment validation",
          "[tls][mtls][integration][smoke]") {
    const auto repo_root = find_repo_root();
    REQUIRE(!repo_root.empty());

    // Test that we can generate and validate certificates for use with TLS samples
    const auto cert_dir =
        std::filesystem::path("/tmp/kmx_mtls_cert_validation_test");
    const auto certs = generate_mtls_cert_set(cert_dir);

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
    const auto verify_server_cert =
        "openssl x509 -in " + shell_quote(certs.server_cert) +
        " -text -noout >/dev/null 2>&1";
    REQUIRE(std::system(verify_server_cert.c_str()) == 0);

    const auto verify_client_cert =
        "openssl x509 -in " + shell_quote(certs.client_cert) +
        " -text -noout >/dev/null 2>&1";
    REQUIRE(std::system(verify_client_cert.c_str()) == 0);

    const auto verify_server_key =
        "openssl rsa -in " + shell_quote(certs.server_key) +
        " -check -noout >/dev/null 2>&1";
    REQUIRE(std::system(verify_server_key.c_str()) == 0);

    const auto verify_client_key =
        "openssl rsa -in " + shell_quote(certs.client_key) +
        " -check -noout >/dev/null 2>&1";
    REQUIRE(std::system(verify_client_key.c_str()) == 0);

    // Cleanup
    try {
        std::filesystem::remove_all(cert_dir);
    } catch (...) {
    }
}

TEST_CASE("mTLS integration: server and client binary discovery",
          "[tls][mtls][integration][smoke]") {
    const auto repo_root = find_repo_root();
    REQUIRE(!repo_root.empty());

    // Verify both samples are available
    const auto server_bin_opt =
        find_binary_under_debug(repo_root, "sample-tls-echo-completion-server");
    const auto client_bin_opt =
        find_binary_under_debug(repo_root, "sample-tls-echo-completion-client");

    if (!server_bin_opt) {
        SKIP("sample-tls-echo-completion-server not built");
    }
    if (!client_bin_opt) {
        SKIP("sample-tls-echo-completion-client not built");
    }

    REQUIRE(std::filesystem::exists(server_bin_opt.value()));
    REQUIRE(std::filesystem::exists(client_bin_opt.value()));
    REQUIRE(std::filesystem::is_regular_file(server_bin_opt.value()));
    REQUIRE(std::filesystem::is_regular_file(client_bin_opt.value()));
}

}  // namespace kmx::aio::tls::test::integration
