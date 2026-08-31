/// @file aio/benchmark/main.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/benchmark/cases.hpp"

#include <charconv>
#include <cstdlib>
#include <print>
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

        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg {argv[i]};
            if (arg == "--help")
            {
                std::println("usage: kmx-aio-benchmark [--filter <substring>] [--scale <factor>] [--repeats <count>]");
                return 0;
            }

            if ((arg == "--filter") && ((i + 1) < argc))
                filter = argv[++i];
            else if ((arg == "--scale") && ((i + 1) < argc))
                scale = std::strtod(argv[++i], nullptr);
            else if ((arg == "--repeats") && ((i + 1) < argc))
                repeats = static_cast<std::size_t>(std::strtoul(argv[++i], nullptr, 10));
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
        register_core_cases(reg);
        register_baseline_cases(reg);
        register_readiness_cases(reg);
        register_completion_cases(reg);

        std::vector<result> results {};
        for (const auto& item: reg.cases())
        {
            if (!filter.empty() && (item.name.find(filter) == std::string_view::npos))
                continue;

            // Every case is run several times and the fastest run is kept: a slower one only ever
            // means the machine was doing something else as well, which is not what is being measured.
            auto best = item.run(scale);
            for (std::size_t r = 1u; r < repeats; ++r)
            {
                auto next = item.run(scale);
                if (!next.skipped && (next.mean_ns < best.mean_ns))
                    best = std::move(next);
            }

            results.push_back(std::move(best));
        }

        print_results(results);
        return 0;
    }
} // namespace kmx::aio::benchmark

int main(const int argc, char** const argv)
{
    return kmx::aio::benchmark::main(argc, argv);
}
