/// @file aio/integration/quic_http3_smoke_test.cpp
/// @brief Completion QUIC HTTP3 smoke test validating handshake -> stream -> response -> close sequence.

#if defined(KMX_AIO_FEATURE_QUIC)

    #include <catch2/catch_test_macros.hpp>
    #include <catch2/generators/catch_generators.hpp>

    #include <sys/wait.h>

    #include <chrono>
    #include <cstdlib>
    #include <filesystem>
    #include <fstream>
    #include <optional>
    #include <string>
    #include <string_view>
    #include <vector>

namespace kmx::aio::quic::test::integration
{
    namespace fs = std::filesystem;

    enum class quic_engine_case
    {
        completion_http3,
        readiness_echo,
    };

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

    [[nodiscard]] static auto contains_markers_in_order(const std::string_view text,
                                                        const std::initializer_list<std::string_view> markers) -> bool
    {
        std::size_t pos = 0;
        for (const auto marker: markers)
        {
            const std::size_t found = text.find(marker, pos);
            if (found == std::string_view::npos)
                return false;
            pos = found + marker.size();
        }

        return true;
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

    [[nodiscard]] static auto find_spdk_runtime_dir(const fs::path& repo_root) -> std::optional<fs::path>
    {
        const std::vector<fs::path> search_roots = {
            repo_root / "build",
            repo_root / "debug",
            repo_root / "default",
        };

        for (const auto& root: search_roots)
        {
            if (!fs::exists(root) || !fs::is_directory(root))
                continue;

            for (const auto& entry: fs::recursive_directory_iterator(root))
                if (entry.is_regular_file() && entry.path().filename() == "libspdk_env_dpdk.so.15.0")
                    return entry.path().parent_path();
        }

        return std::nullopt;
    }

    TEST_CASE("QUIC smoke handshake-stream-response-close parametrized", "[quic][http3][readiness][integration][smoke][slow]")
    {
        const auto engine_case = GENERATE(quic_engine_case::completion_http3, quic_engine_case::readiness_echo);

        const auto repo_root_opt = find_repo_root();
        REQUIRE(repo_root_opt.has_value());

        const fs::path repo_root = *repo_root_opt;
        const bool is_completion = engine_case == quic_engine_case::completion_http3;
        const auto server_bin_name = is_completion ? "sample-quic-http3-server" : "sample-quic-echo-readiness-server";
        const auto client_bin_name = is_completion ? "sample-quic-http3-client" : "sample-quic-echo-readiness-client";
        const auto server_bin_opt = find_binary_under_debug(repo_root, server_bin_name);
        const auto client_bin_opt = find_binary_under_debug(repo_root, client_bin_name);
        const auto spdk_runtime_dir_opt = find_spdk_runtime_dir(repo_root);

        if (!server_bin_opt.has_value() || !client_bin_opt.has_value() || !spdk_runtime_dir_opt.has_value())
            SKIP("QUIC smoke skipped: build readiness/completion QUIC sample binaries first");

        const fs::path cert_path = "/tmp/quic_cert.pem";
        const fs::path key_path = "/tmp/quic_key.pem";
        if (!fs::exists(cert_path) || !fs::exists(key_path))
            SKIP("QUIC smoke skipped: missing /tmp/quic_cert.pem or /tmp/quic_key.pem");

        const auto now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::uint16_t test_port = is_completion ? static_cast<std::uint16_t>(30000u + static_cast<std::uint16_t>(now_ns % 10000u)) :
                                                        static_cast<std::uint16_t>(20000u + static_cast<std::uint16_t>(now_ns % 20000u));
        const fs::path server_log =
            fs::path("/tmp") /
            ((is_completion ? "kmx_http3_server_smoke_" : "kmx_quic_readiness_echo_server_smoke_") + std::to_string(now_ns) + ".log");
        const fs::path client_log =
            fs::path("/tmp") /
            ((is_completion ? "kmx_http3_client_smoke_" : "kmx_quic_readiness_echo_client_smoke_") + std::to_string(now_ns) + ".log");
        const std::string port_env = (is_completion ? "KMX_QUIC_HTTP3_PORT=" : "KMX_QUIC_ECHO_PORT=") + std::to_string(test_port);
        const std::string ld_library_path = "LD_LIBRARY_PATH=/opt/gcc-16/lib64:" + spdk_runtime_dir_opt->string() + ":${LD_LIBRARY_PATH:-}";

        const std::string server_cmd = "env " + port_env + " " + ld_library_path + " stdbuf -oL -eL " +
                                       shell_quote(server_bin_opt->string()) + " > " + shell_quote(server_log.string()) + " 2>&1";
        const std::string client_cmd = "timeout 5s env " + port_env + " " + ld_library_path + " stdbuf -oL -eL " +
                                       shell_quote(client_bin_opt->string()) + " > " + shell_quote(client_log.string()) + " 2>&1";

        const std::string script = "set -u -o pipefail; " + server_cmd + " & " +
                                   "srv=$!; "
                                   "sleep 1; " +
                                   client_cmd +
                                   "; "
                                   "client_rc=$?; "
                                   "kill \"$srv\" >/dev/null 2>&1 || true; "
                                   "wait \"$srv\" >/dev/null 2>&1 || true; "
                                   "exit \"$client_rc\"";

        const std::string full_cmd = "bash -lc " + shell_quote(script);
        const int run_rc = std::system(full_cmd.c_str());

        REQUIRE(run_rc != -1);
        REQUIRE(WIFEXITED(run_rc));

        const int client_exit = WEXITSTATUS(run_rc);
        const auto client_text = read_file_text(client_log);
        const auto server_text = read_file_text(server_log);

        INFO("client log path: " << client_log.string());
        INFO("server log path: " << server_log.string());
        INFO("engine_case: " << (is_completion ? "completion_http3" : "readiness_echo"));
        INFO("client exit code: " << client_exit);
        INFO("client log:\n" << client_text);
        INFO("server log:\n" << server_text);

        REQUIRE(client_exit == 0);

        if (is_completion)
        {
            REQUIRE(contains_markers_in_order(client_text, {
                                                               "on_hsk_done called",
                                                               "[HTTP/3 Client] Received Server Response:",
                                                               "on_conn_closed called, status=8 (LSCONN_ST_CLOSED)",
                                                           }));

            REQUIRE(contains_markers_in_order(server_text, {
                                                               "[HTTP/3 Server] Parsed request method=GET target=/ authority=localhost",
                                                               "on_conn_closed called, status=8 (LSCONN_ST_CLOSED)",
                                                           }));
        }
        else
        {
            REQUIRE(contains_markers_in_order(client_text, {
                                                               "on_hsk_done called",
                                                               "[QUIC Readiness Echo Client] Response #1",
                                                               "[QUIC Readiness Echo Client] Response #2",
                                                               "on_conn_closed called, status=8 (LSCONN_ST_CLOSED)",
                                                           }));

            REQUIRE(contains_markers_in_order(server_text, {
                                                               "Received QUIC stream data:",
                                                               "on_conn_closed called, status=8 (LSCONN_ST_CLOSED)",
                                                           }));
        }
    }
} // namespace kmx::aio::quic::test::integration

#endif // KMX_AIO_FEATURE_QUIC
