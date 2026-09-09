/// @file aio/benchmark/main.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/benchmark/cases.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace kmx::aio::benchmark
{
    /// @brief Parses the command line and runs the matching cases.
    /// @param argc Argument count.
    /// @param argv Argument values.
    /// @return Process exit status.
    /// @throws std::bad_alloc if the case list cannot be built.
    /// @brief Runs one case and stamps it with where it sits in a pairing.
    /// @details The case function measures and does not need to know it is being compared, so the
    ///          pairing is attached here rather than by the case itself.
    /// @param item The case to run.
    /// @param scale How much work the case should do.
    /// @return The measured result, with its pairing recorded.
    /// @throws std::bad_alloc if the case cannot allocate.
    static result measure(const case_entry& item, const double scale) noexcept(false)
    {
        auto out = item.run(scale);
        out.pair_key = item.pair_key;
        out.model = item.model;
        return out;
    }

    /// @brief What the command line asked for.
    struct options
    {
        /// @brief Only cases whose name contains this are run; empty runs them all.
        std::string_view filter {};
        /// @brief Multiplies each case's work; anything at or below zero means the default.
        double scale = 1.0;
        /// @brief How many times each case is measured; the fastest run is kept.
        std::size_t repeats = 3u;
        /// @brief Whether to report JSON rather than a table.
        bool as_json {};
        /// @brief Where to write the report; empty writes to stdout.
        std::string_view output_path {};
    };

    /// @brief Prints the usage text.
    void print_usage() noexcept(false)
    {
        std::println("usage: kmx-aio-benchmark [--filter <substring>] [--scale <factor>] [--repeats <count>]");
        std::println("                         [--format table|json] [--output <path>]");
        std::println("");
        std::println("--output writes the report to a file instead of stdout. The library logs to stdout as it");
        std::println("starts an executor, so JSON asked for without it comes back with log lines through it.");
    }

    /// @brief Reads the --format argument.
    /// @param text The format name.
    /// @param as_json Set when JSON was asked for.
    /// @return `false` when the name is not one this build knows.
    [[nodiscard]] bool parse_format(const std::string_view text, bool& as_json) noexcept(false)
    {
        if (text == "json")
        {
            as_json = true;
            return true;
        }
        if (text == "table")
            return true;

        std::println(stderr, "unknown format: {}", text);
        return false;
    }

    /// @brief Reads the command line.
    /// @param argc The argument count.
    /// @param argv The arguments.
    /// @param out Receives what was asked for.
    /// @return The process exit code to use, or nothing to carry on running.
    [[nodiscard]] std::optional<int> parse_options(const int argc, char** const argv, options& out) noexcept(false)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg {argv[i]};
            if (arg == "--help")
            {
                print_usage();
                return 0;
            }

            const bool has_value = (i + 1) < argc;
            if ((arg == "--filter") && has_value)
                out.filter = argv[++i];
            else if ((arg == "--scale") && has_value)
                out.scale = std::strtod(argv[++i], nullptr);
            else if ((arg == "--repeats") && has_value)
                out.repeats = static_cast<std::size_t>(std::strtoul(argv[++i], nullptr, 10));
            else if ((arg == "--output") && has_value)
                out.output_path = argv[++i];
            else if ((arg == "--format") && has_value)
            {
                if (!parse_format(argv[++i], out.as_json))
                    return 2;
            }
            else
            {
                std::println(stderr, "unknown argument: {}", arg);
                return 2;
            }
        }

        // A nonsensical value is corrected rather than refused: the suite still has something to say.
        out.scale = (out.scale > 0.0) ? out.scale : 1.0;
        out.repeats = std::max(out.repeats, std::size_t {1u});
        return {};
    }

    /// @brief Registers every case this build knows.
    void register_all_cases(registry& reg) noexcept(false)
    {
        register_paired_cases(reg);
        register_core_cases(reg);
        register_baseline_cases(reg);
        register_readiness_cases(reg);
        register_completion_cases(reg);
        register_tls_cases(reg);
        register_http_cases(reg);
        register_single_model_cases(reg);
    }

    /// @brief Groups the selected cases so both sides of a paired scenario run together.
    /// @param selected The cases to run, in registration order.
    /// @return One group per unit of work, holding indices into @p selected.
    /// @details The two sides of a scenario are registered from different files and are nowhere near
    ///          each other in the case list, so they are collected into one unit here and measured
    ///          alternately. Running all of one side's repeats and then the other's would hand whatever
    ///          else the machine was doing during the first half to that side alone - which is
    ///          indistinguishable in the report from the executors genuinely differing, and is exactly
    ///          the mistake this suite exists to avoid making.
    [[nodiscard]] std::vector<std::vector<std::size_t>> group_into_units(const std::vector<const case_entry*>& selected)
    {
        std::vector<std::vector<std::size_t>> units {};
        std::vector<std::string_view> unit_keys {};
        for (std::size_t i {}; i != selected.size(); ++i)
        {
            const auto key = selected[i]->pair_key;
            const auto existing = key.empty() ? unit_keys.end() : std::find(unit_keys.begin(), unit_keys.end(), key);
            if (existing != unit_keys.end())
            {
                units[static_cast<std::size_t>(existing - unit_keys.begin())].push_back(i);
                continue;
            }

            units.push_back({i});
            unit_keys.push_back(key);
        }
        return units;
    }

    /// @brief Measures every selected case, keeping each one's fastest run.
    /// @param selected The cases to measure.
    /// @param opts What the command line asked for.
    /// @return One result per case, in registration order.
    /// @note The fastest run is kept because a slower one only ever means the machine was doing
    ///       something else as well, which is not what is being measured.
    [[nodiscard]] std::vector<result> measure_all(const std::vector<const case_entry*>& selected, const options& opts)
    {
        std::vector<result> results(selected.size());
        for (const auto& unit: group_into_units(selected))
        {
            for (const auto index: unit)
                results[index] = measure(*selected[index], opts.scale);

            for (std::size_t r = 1u; r < opts.repeats; ++r)
                for (const auto index: unit)
                {
                    auto next = measure(*selected[index], opts.scale);
                    if (!next.skipped && (next.mean_ns < results[index].mean_ns))
                        results[index] = std::move(next);
                }
        }
        return results;
    }

    /// @brief Writes the report where the command line asked for it.
    /// @param results What was measured.
    /// @param reg The registry the cases came from.
    /// @param opts What the command line asked for.
    /// @return The process exit code.
    /// @note The file is opened only once there is something to write, so a run that fails earlier
    ///       leaves no truncated file behind for the merge step to read as a complete one.
    [[nodiscard]] int write_report(const std::vector<result>& results, const registry& reg, const options& opts) noexcept(false)
    {
        std::FILE* out = stdout;
        std::unique_ptr<std::FILE, int (*)(std::FILE*)> owned {nullptr, std::fclose};
        if (!opts.output_path.empty())
        {
            const std::string path {opts.output_path};
            owned.reset(std::fopen(path.c_str(), "w"));
            if (!owned)
            {
                std::println(stderr, "cannot write to {}", path);
                return 2;
            }

            out = owned.get();
        }

        if (opts.as_json)
        {
            print_json(results, out);
            return 0;
        }

        print_results(results, reg);
        print_comparison(results, reg);
        return 0;
    }

    static int main(const int argc, char** const argv) noexcept(false)
    {
        options opts {};
        if (const auto code = parse_options(argc, argv, opts); code.has_value())
            return *code;

        registry reg {};
        register_all_cases(reg);

        std::vector<const case_entry*> selected {};
        for (const auto& item: reg.cases())
            if (opts.filter.empty() || (item.name.find(opts.filter) != std::string_view::npos))
                selected.push_back(&item);

        return write_report(measure_all(selected, opts), reg, opts);
    }
} // namespace kmx::aio::benchmark

int main(const int argc, char** const argv)
{
    return kmx::aio::benchmark::main(argc, argv);
}
