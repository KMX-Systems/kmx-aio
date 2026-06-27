#include <kmx/aio/sample/spdk/discovery/manager.hpp>

#include <cstddef>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/spdk/device.hpp>
#include <kmx/aio/completion/spdk/runtime.hpp>
#include <kmx/logger.hpp>

namespace kmx::aio::sample::spdk::discovery
{
    auto collect_requested(const int argc, const char** argv) -> std::vector<std::string>
    {
        if (argc <= 1)
            return {};

        std::vector<std::string> out {};
        out.reserve(static_cast<std::size_t>(argc - 1));

        for (int i = 1; i < argc; ++i)
        {
            if (!argv[i])
                continue;

            const std::string_view name {argv[i]};
            if (!name.empty())
                out.emplace_back(name);
        }

        return out;
    }

    auto run_discovery(int argc, const char** argv) -> int
    {
        auto exec = std::make_shared<kmx::aio::completion::executor>();
        const auto requested = collect_requested(argc, argv);

        const auto enumerate_result = kmx::aio::completion::spdk::runtime::enumerate_bdevs();
        if (!enumerate_result)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SPDK bdev enumeration failed: {}",
                             enumerate_result.error().message());
            return 1;
        }

        const auto& available = *enumerate_result;
        if (available.empty())
        {
            kmx::logger::log(kmx::logger::level::warn, std::source_location::current(),
                             "SPDK initialized but no bdevs are currently registered.");
            return 1;
        }

        kmx::logger::log(kmx::logger::level::info, std::source_location::current(),
                         "Discovered {} registered SPDK bdev(s):", available.size());

        for (const auto& name: available)
        {
            kmx::logger::log(kmx::logger::level::info, std::source_location::current(), "  - {}", name);
        }

        if (requested.empty())
            return 0;

        std::unordered_set<std::string> available_set {available.begin(), available.end()};
        std::vector<std::string> targets {};
        targets.reserve(requested.size());

        for (const auto& candidate: requested)
        {
            if (available_set.contains(candidate))
                targets.emplace_back(candidate);
            else
                kmx::logger::log(kmx::logger::level::warn, std::source_location::current(),
                                 "Requested bdev '{}' is not in registered list.", candidate);
        }

        if (targets.empty())
        {
            kmx::logger::log(kmx::logger::level::warn, std::source_location::current(),
                             "No requested bdevs matched currently registered names.");
            return 1;
        }

        kmx::logger::log(kmx::logger::level::info, std::source_location::current(), "Validating {} requested SPDK bdev(s)...",
                         targets.size());

        std::vector<std::string> found {};

        for (const auto& candidate: targets)
        {
            kmx::aio::completion::spdk::device_config cfg {
                .bdev_name = candidate,
                .block_size = 4096u,
                .block_count = 1u,
            };

            auto create_result = kmx::aio::completion::spdk::device::create(exec, cfg);
            if (create_result)
            {
                kmx::logger::log(kmx::logger::level::info, std::source_location::current(), "Found bdev: {}", candidate);
                found.emplace_back(candidate);
                continue;
            }

            kmx::logger::log(kmx::logger::level::debug, std::source_location::current(), "Probe failed for {}: {}", candidate,
                             create_result.error().message());
        }

        if (found.empty())
        {
            kmx::logger::log(kmx::logger::level::warn, std::source_location::current(), "No requested SPDK bdevs could be opened.");
            kmx::logger::log(kmx::logger::level::info, std::source_location::current(),
                             "Tip: run without args to list registered bdev names.");
            kmx::logger::log(kmx::logger::level::info, std::source_location::current(), "Validation usage: {} <bdev1> <bdev2>", argv[0]);
            return 1;
        }

        kmx::logger::log(kmx::logger::level::info, std::source_location::current(), "Discovered {} SPDK bdev candidate(s).", found.size());

        // Release hardware resources cleanly via spdk lifecycle
        if (auto fini = kmx::aio::completion::spdk::runtime::finalize(); !fini)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SPDK finalize failed: {}",
                             fini.error().message());
        }

        return 0;
    }
}
