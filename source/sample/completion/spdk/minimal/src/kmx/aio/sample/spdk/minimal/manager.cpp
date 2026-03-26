#include <kmx/aio/sample/spdk/minimal/manager.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <memory>
#include <source_location>
#include <string_view>
#include <string>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/spdk/device.hpp>
#include <kmx/aio/completion/spdk/runtime.hpp>
#include <kmx/aio/task.hpp>
#include <kmx/logger.hpp>

namespace kmx::aio::sample::spdk::minimal
{
    auto read_nr_hugepages() -> std::uint64_t
    {
        std::ifstream in("/proc/sys/vm/nr_hugepages");
        std::uint64_t value = 0u;
        in >> value;
        return value;
    }

    void log_spdk_runtime_hints(const std::string_view bdev_name)
    {
        if (bdev_name == "kmx-spdk-fallback")
            return;

        const auto hugepages = read_nr_hugepages();
        if (hugepages == 0u)
        {
            kmx::logger::log(kmx::logger::level::warn, std::source_location::current(),
                             "SPDK runtime hint: no hugepages are configured (nr_hugepages=0). "
                             "Run: echo 1024 | sudo tee /proc/sys/vm/nr_hugepages");
        }

        kmx::logger::log(kmx::logger::level::warn, std::source_location::current(),
                         "SPDK runtime hint: ensure SPDK libs are discoverable at runtime. "
                         "Either run `sudo ldconfig` after install or start with "
                         "`LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH`.");
    }

    auto run_spdk_probe(std::shared_ptr<kmx::aio::completion::executor> exec, std::shared_ptr<std::atomic_bool> ok,
                        std::string bdev_name) -> kmx::aio::task<void>
    {
        kmx::aio::completion::spdk::device_config config {
            .bdev_name = bdev_name,
            .block_size = 4096u,
            .block_count = 128u,
        };

        auto device_result = kmx::aio::completion::spdk::device::create(exec, config);
        if (!device_result)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SPDK device create failed: {}",
                             device_result.error().message());
            log_spdk_runtime_hints(bdev_name);
            exec->stop();
            co_return;
        }

        auto device = std::move(*device_result);

        std::array<std::byte, 4096u> write_block {};
        std::array<std::byte, 4096u> read_block {};

        for (std::size_t i = 0u; i < write_block.size(); ++i)
            write_block[i] = static_cast<std::byte>(i & 0xFFu);

        const auto write_result = co_await device.write(0u, write_block);
        if (!write_result)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SPDK write failed: {}",
                             write_result.error().message());
            exec->stop();
            co_return;
        }

        const auto read_result = co_await device.read(0u, read_block);
        if (!read_result)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SPDK read failed: {}",
                             read_result.error().message());
            exec->stop();
            co_return;
        }

        const auto flush_result = co_await device.flush();
        if (!flush_result)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SPDK flush failed: {}",
                             flush_result.error().message());
            exec->stop();
            co_return;
        }

        if (std::equal(write_block.begin(), write_block.end(), read_block.begin()))
        {
            ok->store(true, std::memory_order_relaxed);
            kmx::logger::log(kmx::logger::level::info, std::source_location::current(), "SPDK minimal sample completed successfully");
        }
        else
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SPDK round-trip mismatch");
        }

        exec->stop();
    }
}
