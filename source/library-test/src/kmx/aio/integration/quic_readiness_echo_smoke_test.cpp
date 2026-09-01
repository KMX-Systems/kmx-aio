/// @file aio/integration/quic_readiness_echo_smoke_test.cpp
/// @brief Readiness QUIC echo smoke test validating handshake -> multi-stream responses -> close.

#if defined(KMX_AIO_FEATURE_QUIC)

    #include <catch2/catch_test_macros.hpp>

    #include <kmx/aio/test/tls_certs.hpp>

    #include <kmx/aio/test/sample_process.hpp>

    #include <chrono>
    #include <cstdlib>
    #include <filesystem>
    #include <fstream>
    #include <optional>
    #include <string>
    #include <string_view>
    #include <vector>

namespace kmx::aio::test::integration::quic_readiness_echo_smoke_test
{
    namespace fs = std::filesystem;

    TEST_CASE("readiness QUIC echo handshake-stream-response-close smoke", "[quic][readiness][integration][smoke][slow]")
    {
        const auto repo_root_opt = find_repo_root();
        REQUIRE(repo_root_opt.has_value());

        const fs::path repo_root = *repo_root_opt;
        const auto server_bin_opt = find_binary_under_debug(repo_root, "sample-quic-echo-readiness-server");
        const auto client_bin_opt = find_binary_under_debug(repo_root, "sample-quic-echo-readiness-client");

        if (!server_bin_opt.has_value() || !client_bin_opt.has_value())
            SKIP("Readiness QUIC echo smoke skipped: build sample-quic-echo-readiness-server and sample-quic-echo-readiness-client first");

        if (!ensure_self_signed_pair("/tmp/quic_cert.pem", "/tmp/quic_key.pem"))
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
            const std::uint16_t test_port = static_cast<std::uint16_t>(20000u + static_cast<std::uint16_t>(attempt_seed % 10000u));
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

            last_attempt.client_log = client_log;
            last_attempt.server_log = server_log;
            last_attempt.run_exited = (run_rc != -1) && WIFEXITED(run_rc);
            last_attempt.client_exit = last_attempt.run_exited ? WEXITSTATUS(run_rc) : -1;
            last_attempt.client_text = read_file_text(client_log);
            last_attempt.server_text = read_file_text(server_log);

            const bool client_ok = last_attempt.client_exit == 0;
            const bool client_markers_ok =
                contains_markers_in_order(last_attempt.client_text, {
                                                                        "on_hsk_done called",
                                                                        "[QUIC Readiness Echo Client] Response #1",
                                                                        "[QUIC Readiness Echo Client] Response #2",
                                                                        "on_conn_closed called, status=8 (LSCONN_ST_CLOSED)",
                                                                    });
            const bool server_markers_ok =
                contains_markers_in_order(last_attempt.server_text, {
                                                                        "Received QUIC stream data:",
                                                                        "on_conn_closed called, status=8 (LSCONN_ST_CLOSED)",
                                                                    });

            if (!attempt_summary.empty())
                attempt_summary += "\n\n";

            attempt_summary +=
                "attempt #" + std::to_string(attempt + 1) + ": exit=" + std::to_string(last_attempt.client_exit) +
                ", client_markers=" + (client_markers_ok ? "yes" : "no") + ", server_markers=" + (server_markers_ok ? "yes" : "no") +
                "\nclient log path: " + last_attempt.client_log.string() + "\nserver log path: " + last_attempt.server_log.string() +
                "\nclient log:\n" + last_attempt.client_text + "\nserver log:\n" + last_attempt.server_text;

            if (client_ok && client_markers_ok && server_markers_ok)
            {
                success = true;
                break;
            }
        }

        INFO(attempt_summary);
        REQUIRE(success);
    }
} // namespace kmx::aio::test::integration::quic_readiness_echo_smoke_test

#endif // KMX_AIO_FEATURE_QUIC
