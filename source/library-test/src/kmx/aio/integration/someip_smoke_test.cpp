/// @file aio/integration/someip_smoke_test.cpp
/// @brief SOME/IP smoke test validating sample echo server/client orchestration.

#if defined(KMX_AIO_FEATURE_SOMEIP)

    #include <catch2/catch_test_macros.hpp>

    #include <kmx/aio/test/sample_process.hpp>

    #include <sys/wait.h>

    #include <chrono>
    #include <cstdlib>
    #include <filesystem>
    #include <fstream>
    #include <optional>
    #include <string>
    #include <string_view>
    #include <vector>

namespace kmx::aio::test::integration::someip_smoke_test
{
    namespace fs = std::filesystem;

    TEST_CASE("someip echo server/client smoke", "[someip][integration][smoke][slow]")
    {
        const auto repo_root_opt = find_repo_root();
        REQUIRE(repo_root_opt.has_value());

        const fs::path repo_root = *repo_root_opt;
        const auto server_bin_opt = find_binary_under_debug(repo_root, "sample-someip-echo-server");
        const auto client_bin_opt = find_binary_under_debug(repo_root, "sample-someip-echo-client");

        if (!server_bin_opt.has_value() || !client_bin_opt.has_value())
            SKIP("SOME/IP smoke skipped: build sample-someip-echo-server and sample-someip-echo-client first");

        const auto now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path server_log = fs::path("/tmp") / ("kmx_someip_echo_server_smoke_" + std::to_string(now_ns) + ".log");
        const fs::path client_log = fs::path("/tmp") / ("kmx_someip_echo_client_smoke_" + std::to_string(now_ns) + ".log");

        const std::string server_cmd = shell_quote(server_bin_opt->string()) + " > " + shell_quote(server_log.string()) + " 2>&1";
        const std::string client_cmd =
            "timeout 5s " + shell_quote(client_bin_opt->string()) + " > " + shell_quote(client_log.string()) + " 2>&1";

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
        INFO("client exit code: " << client_exit);
        INFO("client log:\n" << client_text);
        INFO("server log:\n" << server_text);

        REQUIRE(client_exit == 0);

        REQUIRE(contains_markers_in_order(client_text, {
                                                           "SOMEIP_ECHO_CLIENT_START",
                                                           "SOMEIP_ECHO_CLIENT_DONE",
                                                       }));

        REQUIRE(contains_markers_in_order(server_text, {
                                                           "SOMEIP_ECHO_SERVER_START",
                                                           "SOMEIP_ECHO_SERVER_STOP",
                                                       }));
    }

    TEST_CASE("someip event publisher/subscriber smoke", "[someip][integration][smoke][slow]")
    {
        const auto repo_root_opt = find_repo_root();
        REQUIRE(repo_root_opt.has_value());

        const fs::path repo_root = *repo_root_opt;
        const auto publisher_bin_opt = find_binary_under_debug(repo_root, "sample-someip-event-publisher");
        const auto subscriber_bin_opt = find_binary_under_debug(repo_root, "sample-someip-event-subscriber");

        if (!publisher_bin_opt.has_value() || !subscriber_bin_opt.has_value())
            SKIP("SOME/IP pub/sub smoke skipped: build sample-someip-event-publisher and sample-someip-event-subscriber first");

        const auto now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path publisher_log = fs::path("/tmp") / ("kmx_someip_pub_smoke_" + std::to_string(now_ns) + ".log");
        const fs::path subscriber_log = fs::path("/tmp") / ("kmx_someip_sub_smoke_" + std::to_string(now_ns) + ".log");

        const std::string publisher_cmd = shell_quote(publisher_bin_opt->string()) + " > " + shell_quote(publisher_log.string()) + " 2>&1";
        const std::string subscriber_cmd =
            "timeout 8s " + shell_quote(subscriber_bin_opt->string()) + " > " + shell_quote(subscriber_log.string()) + " 2>&1";

        const std::string script = "set -u -o pipefail; " + publisher_cmd + " & " +
                                   "pub=$!; "
                                   "sleep 1; " +
                                   subscriber_cmd +
                                   "; "
                                   "sub_rc=$?; "
                                   "kill \"$pub\" >/dev/null 2>&1 || true; "
                                   "wait \"$pub\" >/dev/null 2>&1 || true; "
                                   "exit \"$sub_rc\"";

        const std::string full_cmd = "bash -lc " + shell_quote(script);
        const int run_rc = std::system(full_cmd.c_str());

        REQUIRE(run_rc != -1);
        REQUIRE(WIFEXITED(run_rc));

        const int subscriber_exit = WEXITSTATUS(run_rc);
        const auto publisher_text = read_file_text(publisher_log);
        const auto subscriber_text = read_file_text(subscriber_log);

        INFO("publisher log path: " << publisher_log.string());
        INFO("subscriber log path: " << subscriber_log.string());
        INFO("subscriber exit code: " << subscriber_exit);
        INFO("publisher log:\n" << publisher_text);
        INFO("subscriber log:\n" << subscriber_text);

        REQUIRE(subscriber_exit == 0);

        REQUIRE(contains_markers_in_order(publisher_text, {
                                                              "SOMEIP_EVENT_PUBLISHER_START",
                                                              "SOMEIP_EVENT_PUBLISHER_STOP",
                                                          }));

        REQUIRE(contains_markers_in_order(subscriber_text, {
                                                               "SOMEIP_EVENT_SUBSCRIBER_START",
                                                               "SOMEIP_EVENT_SUBSCRIBER_DONE",
                                                           }));
    }

    TEST_CASE("someip diagnostics sample smoke", "[someip][integration][smoke][slow]")
    {
        const auto repo_root_opt = find_repo_root();
        REQUIRE(repo_root_opt.has_value());

        const fs::path repo_root = *repo_root_opt;
        const auto diagnostics_bin_opt = find_binary_under_debug(repo_root, "sample-someip-diagnostics");

        if (!diagnostics_bin_opt.has_value())
            SKIP("SOME/IP diagnostics smoke skipped: build sample-someip-diagnostics first");

        const auto now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path diagnostics_log = fs::path("/tmp") / ("kmx_someip_diag_smoke_" + std::to_string(now_ns) + ".log");

        const std::string diagnostics_cmd =
            "timeout 8s " + shell_quote(diagnostics_bin_opt->string()) + " > " + shell_quote(diagnostics_log.string()) + " 2>&1";
        const std::string full_cmd = "bash -lc " + shell_quote("set -u -o pipefail; " + diagnostics_cmd);
        const int run_rc = std::system(full_cmd.c_str());

        REQUIRE(run_rc != -1);
        REQUIRE(WIFEXITED(run_rc));

        const int diagnostics_exit = WEXITSTATUS(run_rc);
        const auto diagnostics_text = read_file_text(diagnostics_log);

        INFO("diagnostics log path: " << diagnostics_log.string());
        INFO("diagnostics exit code: " << diagnostics_exit);
        INFO("diagnostics log:\n" << diagnostics_text);

        REQUIRE(diagnostics_exit == 0);
        REQUIRE(contains_markers_in_order(diagnostics_text, {
                                                                "SOMEIP_DIAGNOSTICS_DONE",
                                                            }));
    }
} // namespace kmx::aio::test::integration::someip_smoke_test

#endif // KMX_AIO_FEATURE_SOMEIP
