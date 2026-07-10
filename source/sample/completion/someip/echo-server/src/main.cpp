#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <source_location>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/sample/someip/echo_server/manager.hpp>
#include <kmx/logger.hpp>

int main() noexcept
{
    try
    {
        auto exec = std::make_shared<kmx::aio::completion::executor>();
        auto ok = std::make_shared<std::atomic_bool>(false);

        kmx::aio::sample::someip::echo_server::manager mgr({
            .application_name = "kmx_someip_echo_server",
            .config_file_path = "",
            .service_id = 0x1111u,
            .instance_id = 0x2222u,
            .iterate_timeout = std::chrono::milliseconds(10),
        });

        exec->spawn(mgr.run(exec, ok));
        exec->run();
        return ok->load(std::memory_order_relaxed) ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Fatal error: {}", e.what());
        return 1;
    }
}
