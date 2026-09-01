/// @file aio/test/sample_process.hpp
/// @brief Locating and running the built sample binaries from a smoke test.
/// @details The smoke tests do not link the samples; they run them as processes and read what they
///          printed. That means every one of them has to find the repository, find a binary inside a
///          build tree whose layout depends on how it was built, quote paths safely into a shell
///          command, and decode what std::system() returned - none of which is what the test is about.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <chrono>
    #include <cstdlib>
    #include <filesystem>
    #include <fstream>
    #include <initializer_list>
    #include <iterator>
    #include <optional>
    #include <string>
    #include <string_view>
    #include <vector>

    #include <sys/wait.h>
#endif

namespace kmx::aio::test
{
    /// @brief Reads a whole file into a string.
    /// @param path The file to read.
    /// @return Its contents, or an empty string when it could not be opened - which for a log file a
    ///         sample never wrote is the same thing as "it printed nothing".
    [[nodiscard]] inline std::string read_file_text(const std::filesystem::path& path) noexcept(false)
    {
        std::ifstream in(path);
        if (!in.is_open())
            return {};

        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    /// @brief Wraps @p raw so a shell reads it as one literal argument.
    /// @details Single quotes, with embedded quotes spliced as '\'' - the only form that survives
    ///          arbitrary content, including the spaces a build path under a home directory can contain.
    /// @param raw The text to quote.
    /// @return The quoted form, safe to concatenate into a shell command.
    [[nodiscard]] inline std::string shell_quote(const std::string_view raw) noexcept(false)
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

    /// @brief Whether @p text contains every marker, in the order given.
    /// @details Order matters: a sample that printed its "done" line before its "start" line did not do
    ///          what the test is checking, even though both strings are present.
    /// @param text The captured output.
    /// @param markers The substrings to find, in the order they must appear.
    /// @return True when all of them were found in sequence.
    [[nodiscard]] inline bool contains_markers_in_order(const std::string_view text,
                                                        const std::initializer_list<std::string_view> markers) noexcept
    {
        std::size_t pos = 0u;
        for (const auto marker: markers)
        {
            const std::size_t found = text.find(marker, pos);
            if (found == std::string_view::npos)
                return false;

            pos = found + marker.size();
        }

        return true;
    }

    /// @brief Walks up from the working directory looking for the repository root.
    /// @details Keyed on kmx-aio.qbs, since the test binary's own location depends on the build root and
    ///          cannot be relied on to sit anywhere in particular relative to the tree.
    /// @return The root, or nothing when the test is running outside a checkout.
    [[nodiscard]] inline std::optional<std::filesystem::path> find_repo_root() noexcept(false)
    {
        namespace fs = std::filesystem;

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

    /// @brief Finds a built sample binary somewhere under the debug build trees.
    /// @details Three locations are searched because three are used: where the scripts build (see
    ///          script/feature/common.sh), and the two in-tree spots a bare "qbs build" leaves behind
    ///          depending on the directory it was run from. When the same binary exists in more than one
    ///          - which is what happens after building the same sample two different ways - the newest
    ///          is the one the test meant, so a stale tree does not silently take precedence.
    /// @param repo_root The repository root, from @ref find_repo_root.
    /// @param binary_name The file name to look for.
    /// @return The newest match, or nothing when the sample was not built.
    [[nodiscard]] inline std::optional<std::filesystem::path> find_binary_under_debug(const std::filesystem::path& repo_root,
                                                                                      const std::string_view binary_name) noexcept(false)
    {
        namespace fs = std::filesystem;

        const std::vector<fs::path> debug_dirs = {
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
                if (!entry.is_regular_file() || (entry.path().filename() != binary_name))
                    continue;

                const auto mtime = entry.last_write_time();
                if (!newest_path.has_value() || (mtime > newest_mtime))
                {
                    newest_path = entry.path();
                    newest_mtime = mtime;
                }
            }
        }

        return newest_path;
    }

    /// @brief Runs @p script under bash and reports the exit code it finished with.
    /// @details `std::system` returns a wait status, not an exit code, and the difference matters here:
    ///          a sample killed by a signal and a sample that exited non-zero are different failures,
    ///          and a status read as an exit code turns the first into a meaningless number.
    /// @param script The shell script to run.
    /// @return The exit code, or nothing when the shell could not be started or did not exit normally.
    [[nodiscard]] inline std::optional<int> run_shell(const std::string& script) noexcept(false)
    {
        const std::string full_cmd = "bash -lc " + shell_quote(script);
        const int status = std::system(full_cmd.c_str());
        if ((status == -1) || (WIFEXITED(status) == 0))
            return std::nullopt;

        return WEXITSTATUS(status);
    }

    /// @brief A log path nothing else in the run will pick.
    /// @details Two smoke tests in one binary, or two runs of the same one, otherwise write over each
    ///          other's output and each reads back the other's - which reads as a flake.
    /// @param prefix Names the test the log belongs to.
    /// @return A path under the system temp directory.
    [[nodiscard]] inline std::filesystem::path unique_log_path(const std::string& prefix) noexcept(false)
    {
        const auto now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() / (prefix + "_" + std::to_string(now_ns) + ".log");
    }

    /// @brief The LD_LIBRARY_PATH assignment a sample built with the gcc16 profile needs.
    /// @details Without it such a binary dies at startup on `GLIBCXX_3.4.35' not found, which a smoke
    ///          test would otherwise report as the sample failing rather than never having started.
    /// @return An `env`-style assignment, ready to place before a command.
    [[nodiscard]] inline std::string gcc16_library_path() noexcept(false)
    {
        return "LD_LIBRARY_PATH=/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-}";
    }

} // namespace kmx::aio::test
