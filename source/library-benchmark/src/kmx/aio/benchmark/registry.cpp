/// @file src/kmx/aio/benchmark/registry.cpp
/// @brief Benchmark case registry implementation: cases, pairings and group descriptions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/benchmark/registry.hpp>
#ifndef PCH
    #include <string_view>
#endif

namespace kmx::aio::benchmark
{
    void registry::add(const std::string_view name, const case_fn_t run) noexcept(false)
    {
        cases_.push_back(case_entry {name, run});
    }

    void registry::add_paired(const std::string_view key, const execution_model model, const std::string_view name,
                              const case_fn_t run) noexcept(false)
    {
        cases_.push_back(case_entry {name, run, key, model});

        // The scenario is listed the first time either side mentions it, so the comparison keeps the
        // order the cases were registered in whichever side got there first.
        for (const auto& item: pairs_)
            if (item.key == key)
                return;

        pairs_.push_back(pair_entry {key, {}});
    }

    void registry::describe_pair(const std::string_view key, const std::string_view description) noexcept(false)
    {
        for (auto& item: pairs_)
            if (item.key == key)
            {
                item.description = description;
                return;
            }

        pairs_.push_back(pair_entry {key, description});
    }

    void registry::describe(const std::string_view group, const std::string_view description) noexcept(false)
    {
        groups_.push_back(group_entry {group, description});
    }

    std::string_view registry::description(const std::string_view group) const noexcept
    {
        for (const auto& item: groups_)
            if (item.name == group)
                return item.description;

        return {};
    }

    std::string_view registry::pair_description(const std::string_view key) const noexcept
    {
        for (const auto& item: pairs_)
            if (item.key == key)
                return item.description;

        return {};
    }
}
