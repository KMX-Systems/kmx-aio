/// @file aio/integration/gpu_image_processing_smoke_test.cpp
/// @brief GPU image processing sample smoke test.

#if defined(KMX_AIO_FEATURE_CUDA)

    #include <catch2/catch_test_macros.hpp>

    #include <sys/wait.h>
    #include <unistd.h>

    #include <cstdlib>
    #include <filesystem>
    #include <fstream>
    #include <optional>
    #include <string>
    #include <string_view>

namespace kmx::aio::gpu::test::integration
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
        const fs::path debug_dir = repo_root / "source" / "debug";
        if (!fs::exists(debug_dir) || !fs::is_directory(debug_dir))
            return std::nullopt;

        for (const auto& entry: fs::recursive_directory_iterator(debug_dir))
        {
            if (entry.is_regular_file() && entry.path().filename() == binary_name)
                return entry.path();
        }

        return std::nullopt;
    }

    TEST_CASE("gpu image processing sample smoke", "[gpu][integration][smoke][slow]")
    {
        const auto repo_root_opt = find_repo_root();
        REQUIRE(repo_root_opt.has_value());

        const fs::path repo_root = *repo_root_opt;
        const auto sample_bin_opt = find_binary_under_debug(repo_root, "sample-gpu-image-processing");
        if (!sample_bin_opt.has_value())
            SKIP("GPU sample smoke skipped: build sample-gpu-image-processing first");

        INFO("sample binary: " << sample_bin_opt->string());

        const fs::path run_log = fs::path("/tmp") / ("kmx_gpu_image_processing_smoke_" + std::to_string(::getpid()) + ".log");

        const std::string cmd = "env LD_LIBRARY_PATH=/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-} " + shell_quote(sample_bin_opt->string()) +
                                " --max-frames 1 > " + shell_quote(run_log.string()) + " 2>&1";
        const std::string full_cmd = "bash -lc " + shell_quote(cmd);
        const int run_rc = std::system(full_cmd.c_str());

        REQUIRE(run_rc != -1);
        REQUIRE(WIFEXITED(run_rc));
        REQUIRE(WEXITSTATUS(run_rc) == 0);

        const auto run_text = read_file_text(run_log);
        INFO("run log path: " << run_log.string());
        INFO("run log:\n" << run_text);

        REQUIRE(run_text.find("[GPU Image Processing] frame_bytes=") != std::string::npos);
        REQUIRE(run_text.find("[GPU Image Processing] tasks_spawned=") != std::string::npos);
    }
} // namespace kmx::aio::gpu::test::integration

#endif // KMX_AIO_FEATURE_CUDA
