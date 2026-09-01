/// @file aio/benchmark/main.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/benchmark/cases.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
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
    static int main(const int argc, char** const argv) noexcept(false)
    {
        std::string_view filter {};
        double scale = 1.0;
        std::size_t repeats = 3u;
        bool as_json = false;
        std::string_view output_path {};

        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg {argv[i]};
            if (arg == "--help")
            {
                std::println("usage: kmx-aio-benchmark [--filter <substring>] [--scale <factor>] [--repeats <count>]");
                std::println("                         [--format table|json] [--output <path>]");
                std::println("");
                std::println("--output writes the report to a file instead of stdout. The library logs to stdout as it");
                std::println("starts an executor, so JSON asked for without it comes back with log lines through it.");
                return 0;
            }

            if ((arg == "--filter") && ((i + 1) < argc))
                filter = argv[++i];
            else if ((arg == "--scale") && ((i + 1) < argc))
                scale = std::strtod(argv[++i], nullptr);
            else if ((arg == "--repeats") && ((i + 1) < argc))
                repeats = static_cast<std::size_t>(std::strtoul(argv[++i], nullptr, 10));
            else if ((arg == "--format") && ((i + 1) < argc))
            {
                const std::string_view format {argv[++i]};
                if (format == "json")
                    as_json = true;
                else if (format != "table")
                {
                    std::println(stderr, "unknown format: {}", format);
                    return 2;
                }
            }
            else if ((arg == "--output") && ((i + 1) < argc))
                output_path = argv[++i];
            else
            {
                std::println(stderr, "unknown argument: {}", arg);
                return 2;
            }
        }

        if (scale <= 0.0)
            scale = 1.0;

        if (repeats == 0u)
            repeats = 1u;

        registry reg {};
        register_paired_cases(reg);
        register_core_cases(reg);
        register_baseline_cases(reg);
        register_readiness_cases(reg);
        register_completion_cases(reg);
        register_tls_cases(reg);
        register_http_cases(reg);
        register_single_model_cases(reg);

        std::vector<const case_entry*> selected {};
        for (const auto& item: reg.cases())
            if (filter.empty() || (item.name.find(filter) != std::string_view::npos))
                selected.push_back(&item);

        // The two sides of a scenario are registered from different files and are nowhere near each
        // other in the case list, so they are collected into one unit here and measured alternately.
        // Running all of one side's repeats and then the other's would hand whatever else the machine
        // was doing during the first half to that side alone - which is indistinguishable in the
        // report from the executors genuinely differing, and is exactly the mistake this suite exists
        // to avoid making.
        std::vector<std::vector<std::size_t>> units {};
        std::vector<std::string_view> unit_keys {};
        for (std::size_t i {}; i != selected.size(); ++i)
        {
            const auto key = selected[i]->pair_key;
            if (key.empty())
            {
                units.push_back({i});
                unit_keys.emplace_back();
                continue;
            }

            const auto existing = std::find(unit_keys.begin(), unit_keys.end(), key);
            if (existing == unit_keys.end())
            {
                units.push_back({i});
                unit_keys.push_back(key);
            }
            else
                units[static_cast<std::size_t>(existing - unit_keys.begin())].push_back(i);
        }

        const auto measure = [scale](const case_entry& item) noexcept(false)
        {
            auto out = item.run(scale);

            // The case function measures and does not need to know it is being compared, so where it
            // sits in a pairing is attached here rather than by the case itself.
            out.pair_key = item.pair_key;
            out.model = item.model;
            return out;
        };

        // Indexed by position in `selected`, so the report comes out in registration order however the
        // units were scheduled.
        std::vector<result> results(selected.size());

        for (const auto& unit: units)
        {
            // Every case is run several times and the fastest run is kept: a slower one only ever
            // means the machine was doing something else as well, which is not what is being measured.
            for (const auto index: unit)
                results[index] = measure(*selected[index]);

            for (std::size_t r = 1u; r < repeats; ++r)
                for (const auto index: unit)
                {
                    auto next = measure(*selected[index]);
                    if (!next.skipped && (next.mean_ns < results[index].mean_ns))
                        results[index] = std::move(next);
                }
        }

        // Opened only now, so a run that fails before it has anything to report leaves no truncated
        // file behind for the merge step to read as a complete one.
        std::FILE* out = stdout;
        std::unique_ptr<std::FILE, int (*)(std::FILE*)> owned {nullptr, std::fclose};
        if (!output_path.empty())
        {
            const std::string path {output_path};
            owned.reset(std::fopen(path.c_str(), "w"));
            if (!owned)
            {
                std::println(stderr, "cannot write to {}", path);
                return 2;
            }

            out = owned.get();
        }

        if (as_json)
        {
            print_json(results, out);
            return 0;
        }

        print_results(results, reg);
        print_comparison(results, reg);
        return 0;
    }
} // namespace kmx::aio::benchmark

int main(const int argc, char** const argv)
{
    return kmx::aio::benchmark::main(argc, argv);
}
