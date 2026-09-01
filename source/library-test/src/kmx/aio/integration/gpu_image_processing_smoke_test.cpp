/// @file aio/integration/gpu_image_processing_smoke_test.cpp
/// @brief GPU image processing sample smoke test.

#if defined(KMX_AIO_FEATURE_CUDA)

    #include <catch2/catch_test_macros.hpp>

    #include <kmx/aio/test/sample_process.hpp>

    #include <sys/wait.h>
    #include <unistd.h>

    #include <cstdlib>
    #include <filesystem>
    #include <fstream>
    #include <optional>
    #include <string>
    #include <string_view>
    #include <vector>

namespace kmx::aio::test::integration::gpu_image_processing_smoke_test
{
    namespace fs = std::filesystem;

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
} // namespace kmx::aio::test::integration::gpu_image_processing_smoke_test

#endif // KMX_AIO_FEATURE_CUDA
