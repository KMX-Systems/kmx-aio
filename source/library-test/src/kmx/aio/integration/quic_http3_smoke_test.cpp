/// @file aio/integration/quic_http3_smoke_test.cpp
/// @brief Completion QUIC HTTP3 smoke test validating handshake -> stream -> response -> close sequence.

#if defined(KMX_AIO_FEATURE_QUIC)

    #include <catch2/catch_test_macros.hpp>
    #include <catch2/generators/catch_generators.hpp>

    #include <chrono>
    #include <cstdlib>
    #include <filesystem>
    #include <fstream>
    #include <optional>
    #include <string>
    #include <string_view>
    #include <vector>

namespace kmx::aio::test::integration::quic_http3_smoke_test
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
    [[nodiscard]] static bool ensure_quic_certificates()
    {
        const fs::path cert_path = "/tmp/quic_cert.pem";
        const fs::path key_path = "/tmp/quic_key.pem";
        if (fs::exists(cert_path) && fs::exists(key_path))
            return true;
        const int ret = std::system("openssl req -x509 -newkey rsa:2048 -keyout /tmp/quic_key.pem -out /tmp/quic_cert.pem "
                                    "-days 1 -nodes -subj \"/CN=localhost\" >/dev/null 2>&1");
        return ret == 0 && fs::exists(cert_path) && fs::exists(key_path);
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
            // Where the scripts build (see script/feature/common.sh), then the two in-tree locations a bare
            // "qbs build" leaves behind depending on the directory it was run from.
            repo_root / "output" / "debug",
            repo_root / "debug",
            repo_root / "source" / "debug",
        };

        std::optional<fs::path> newest_path;
        fs::file_time_type newest_mtime {};

        for (const auto& debug_dir: debug_dirs)
        {
            if (!fs::exists(debug_dir) || !fs::is_directory(debug_dir))
                continue;

            for (const auto& entry: fs::recursive_directory_iterator(debug_dir))
            {
                if (!entry.is_regular_file() || entry.path().filename() != binary_name)
                    continue;

                const auto mtime = entry.last_write_time();
                if (!newest_path.has_value() || mtime > newest_mtime)
                {
                    newest_path = entry.path();
                    newest_mtime = mtime;
                }
            }
        }

        return newest_path;
    }

    /// SPDK is installed into its own prefix rather than next to the sample binaries, so when a build has
    /// the SPDK feature on, the samples need that directory on their library path. It is optional: a build
    /// without SPDK produces samples that never load it. Only the directory is looked for here, not any
    /// particular soname, so an SPDK built at a different version is still found.
    [[nodiscard]] static auto find_spdk_runtime_dir(const fs::path& repo_root) -> std::optional<fs::path>
    {
        const fs::path install_prefix = repo_root / "output" / "spdk-local" / "install-local";
        const std::vector<fs::path> library_dirs = {
            install_prefix / "lib",
            install_prefix / "lib64",
        };

        for (const auto& library_dir: library_dirs)
        {
            if (!fs::exists(library_dir) || !fs::is_directory(library_dir))
                continue;

            for (const auto& entry: fs::directory_iterator(library_dir))
                if (entry.is_regular_file() && entry.path().filename().string().starts_with("libspdk_env_dpdk.so"))
                    return library_dir;
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

        if (!server_bin_opt.has_value() || !client_bin_opt.has_value())
            SKIP(std::string("QUIC smoke skipped: sample binary not built: ") +
                 (server_bin_opt.has_value() ? client_bin_name : server_bin_name));

        if (!ensure_quic_certificates())
            SKIP("QUIC smoke skipped: failed to generate /tmp/quic_cert.pem and /tmp/quic_key.pem");

        const auto now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::uint16_t test_port = is_completion ? static_cast<std::uint16_t>(28000u + static_cast<std::uint16_t>(now_ns % 2000u)) :
                                                        static_cast<std::uint16_t>(20000u + static_cast<std::uint16_t>(now_ns % 2000u));
        const fs::path server_log =
            fs::path("/tmp") /
            ((is_completion ? "kmx_http3_server_smoke_" : "kmx_quic_readiness_echo_server_smoke_") + std::to_string(now_ns) + ".log");
        const fs::path client_log =
            fs::path("/tmp") /
            ((is_completion ? "kmx_http3_client_smoke_" : "kmx_quic_readiness_echo_client_smoke_") + std::to_string(now_ns) + ".log");
        const std::string port_env = (is_completion ? "KMX_QUIC_HTTP3_PORT=" : "KMX_QUIC_ECHO_PORT=") + std::to_string(test_port);
        const std::string ld_library_path = "LD_LIBRARY_PATH=/opt/gcc-16/lib64:" +
                                            (spdk_runtime_dir_opt.has_value() ? spdk_runtime_dir_opt->string() + ":" : std::string {}) +
                                            "${LD_LIBRARY_PATH:-}";

        const std::string server_cmd = "env " + port_env + " " + ld_library_path + " stdbuf -oL -eL " +
                                       shell_quote(server_bin_opt->string()) + " > " + shell_quote(server_log.string()) + " 2>&1";
        const std::string readiness_client_env = is_completion ? "" : " KMX_QUIC_ECHO_CLIENT_CLOSE_AFTER_RESPONSES=2";
        const std::string client_cmd = "timeout 30s env " + port_env + readiness_client_env + " " + ld_library_path + " stdbuf -oL -eL " +
                                       shell_quote(client_bin_opt->string()) + " > " + shell_quote(client_log.string()) + " 2>&1";

        const std::string script =
            "set -u -o pipefail; " + server_cmd + " & " +
            "srv=$!; "
            "port_dec=" +
            std::to_string(test_port) +
            "; "
            "port_hex=$(printf '%04X' " +
            std::to_string(test_port) +
            "); "
            "ready=0; "
            "deadline=$((SECONDS+15)); "
            "while (( SECONDS < deadline )); do "
            "  if ! kill -0 \"$srv\" >/dev/null 2>&1; then break; fi; "
            "  if command -v ss >/dev/null 2>&1; then "
            "    if ss -lunp 2>/dev/null | grep -F \":$port_dec\" | grep -Fq \"pid=$srv,\"; then ready=1; break; fi; "
            "  else "
            "    if grep -qi \":$port_hex \" /proc/net/udp /proc/net/udp6 2>/dev/null; then ready=1; break; fi; "
            "  fi; "
            "  sleep 0.1; "
            "done; "
            "if (( ready == 0 )); then "
            "  client_rc=124; "
            "else "
            "  sleep 1; " +
            client_cmd +
            "; client_rc=$?; "
            "fi; "
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
} // namespace kmx::aio::test::integration::quic_http3_smoke_test

#endif // KMX_AIO_FEATURE_QUIC
