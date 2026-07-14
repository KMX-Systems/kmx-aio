#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <source_location>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/sample/someip/echo_client/manager.hpp>
#include <kmx/logger.hpp>

int main() noexcept
{
    try
    {
        kmx::aio::completion::executor exec;
        auto ok = std::make_shared<std::atomic_bool>(false);

        kmx::aio::sample::someip::echo_client::manager mgr({
            .application_name = "kmx_someip_echo_client",
            .config_file_path = "",
            .service_id = 0x1111u,
            .instance_id = 0x2222u,
            .connect_timeout = std::chrono::milliseconds(500),
            .iterate_timeout = std::chrono::milliseconds(10),
            .reconnect_delay = std::chrono::milliseconds(50),
            .max_reconnect_attempts = 20u,
        });

        exec.spawn(mgr.run(exec, ok));
        exec.run();
        return ok->load(std::memory_order_relaxed) ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Fatal error: {}", e.what());
        return 1;
    }
}
