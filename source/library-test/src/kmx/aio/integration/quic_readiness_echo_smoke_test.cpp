/// @file aio/integration/quic_readiness_echo_smoke_test.cpp
/// @brief Readiness QUIC echo smoke test validating handshake -> multi-stream responses -> close.

#if defined(KMX_AIO_FEATURE_QUIC)

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

namespace kmx::aio::quic::test::integration
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

    TEST_CASE("readiness QUIC echo handshake-stream-response-close smoke", "[quic][readiness][integration][smoke][slow]")
    {
        const auto repo_root_opt = find_repo_root();
        REQUIRE(repo_root_opt.has_value());

        const fs::path repo_root = *repo_root_opt;
        const auto server_bin_opt = find_binary_under_debug(repo_root, "sample-quic-echo-readiness-server");
        const auto client_bin_opt = find_binary_under_debug(repo_root, "sample-quic-echo-readiness-client");

        if (!server_bin_opt.has_value() || !client_bin_opt.has_value())
            SKIP("Readiness QUIC echo smoke skipped: build sample-quic-echo-readiness-server and sample-quic-echo-readiness-client first");

        if (!ensure_quic_certificates())
            SKIP("Readiness QUIC echo smoke skipped: failed to generate /tmp/quic_cert.pem and /tmp/quic_key.pem");

        struct attempt_result
        {
            int client_exit {};
            fs::path client_log {};
            fs::path server_log {};
            std::string client_text {};
            std::string server_text {};
            bool run_exited {};
        };

        const auto now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
        static constexpr int max_attempts = 3;
        bool success = false;
        attempt_result last_attempt {};
        std::string attempt_summary;

        for (int attempt = 0; attempt < max_attempts; ++attempt)
        {
            const auto attempt_seed = now_ns + attempt;
            const std::uint16_t test_port =
                static_cast<std::uint16_t>(20000u + static_cast<std::uint16_t>(attempt_seed % 20000u));
            const fs::path server_log =
                fs::path("/tmp") / ("kmx_quic_readiness_echo_server_smoke_" + std::to_string(attempt_seed) + ".log");
            const fs::path client_log =
                fs::path("/tmp") / ("kmx_quic_readiness_echo_client_smoke_" + std::to_string(attempt_seed) + ".log");
            const std::string port_env = "KMX_QUIC_ECHO_PORT=" + std::to_string(test_port);

            const std::string server_cmd = "env " + port_env + " LD_LIBRARY_PATH=/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-} stdbuf -oL -eL " +
                                           shell_quote(server_bin_opt->string()) + " > " + shell_quote(server_log.string()) + " 2>&1";
            const std::string client_cmd =
                "timeout 30s env " + port_env +
                " KMX_QUIC_ECHO_CLIENT_CLOSE_AFTER_RESPONSES=2 LD_LIBRARY_PATH=/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-} stdbuf -oL -eL " +
                shell_quote(client_bin_opt->string()) + " > " + shell_quote(client_log.string()) + " 2>&1";

            const std::string script = "set -u -o pipefail; " + server_cmd + " & " +
                                       "srv=$!; "
                                       "port_dec=" + std::to_string(test_port) + "; "
                                       "port_hex=$(printf '%04X' " + std::to_string(test_port) + "); "
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

            last_attempt.client_log = client_log;
            last_attempt.server_log = server_log;
            last_attempt.run_exited = (run_rc != -1) && WIFEXITED(run_rc);
            last_attempt.client_exit = last_attempt.run_exited ? WEXITSTATUS(run_rc) : -1;
            last_attempt.client_text = read_file_text(client_log);
            last_attempt.server_text = read_file_text(server_log);

            const bool client_ok = last_attempt.client_exit == 0;
            const bool client_markers_ok = contains_markers_in_order(last_attempt.client_text,
                                                                     {
                                                                         "on_hsk_done called",
                                                                         "[QUIC Readiness Echo Client] Response #1",
                                                                         "[QUIC Readiness Echo Client] Response #2",
                                                                         "on_conn_closed called, status=8 (LSCONN_ST_CLOSED)",
                                                                     });
            const bool server_markers_ok = contains_markers_in_order(last_attempt.server_text,
                                                                     {
                                                                         "Received QUIC stream data:",
                                                                         "on_conn_closed called, status=8 (LSCONN_ST_CLOSED)",
                                                                     });

            if (!attempt_summary.empty())
                attempt_summary += "\n\n";

            attempt_summary += "attempt #" + std::to_string(attempt + 1) + ": exit=" + std::to_string(last_attempt.client_exit) +
                               ", client_markers=" + (client_markers_ok ? "yes" : "no") + ", server_markers=" +
                               (server_markers_ok ? "yes" : "no") + "\nclient log path: " + last_attempt.client_log.string() +
                               "\nserver log path: " + last_attempt.server_log.string() + "\nclient log:\n" + last_attempt.client_text +
                               "\nserver log:\n" + last_attempt.server_text;

            if (client_ok && client_markers_ok && server_markers_ok)
            {
                success = true;
                break;
            }
        }

        INFO(attempt_summary);
        REQUIRE(success);
    }
} // namespace kmx::aio::quic::test::integration

#endif // KMX_AIO_FEATURE_QUIC
