/// @file aio/integration/someip_smoke_test.cpp
/// @brief SOME/IP smoke test validating sample echo server/client orchestration.

#if defined(KMX_AIO_FEATURE_SOMEIP)

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

namespace kmx::aio::someip::test::integration
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
} // namespace kmx::aio::someip::test::integration

#endif // KMX_AIO_FEATURE_SOMEIP
