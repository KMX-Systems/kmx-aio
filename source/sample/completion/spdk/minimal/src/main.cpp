#include <atomic>
#include <exception>
#include <memory>
#include <source_location>
#include <string_view>
#include <string>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/spdk/runtime.hpp>
#include <kmx/logger.hpp>

#include <kmx/aio/sample/spdk/minimal/manager.hpp>

int main(int argc, const char** argv) noexcept
{
    try
    {
        std::string bdev_name = "kmx-spdk-fallback";
        if (argc >= 2 && argv[1] && std::string_view(argv[1]).size() > 0u)
            bdev_name = argv[1];

        auto exec = std::make_shared<kmx::aio::completion::executor>();
        auto ok = std::make_shared<std::atomic_bool>(false);

        exec->spawn(kmx::aio::sample::spdk::minimal::run_spdk_probe(exec, ok, std::move(bdev_name)));
        exec->run();

        // Release hardware resources cleanly via spdk lifecycle
        if (auto fini = kmx::aio::completion::spdk::runtime::finalize(); !fini)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SPDK finalize failed: {}",
                             fini.error().message());
        }

        return ok->load(std::memory_order_relaxed) ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Fatal error: {}", e.what());
        return 1;
    }
}
