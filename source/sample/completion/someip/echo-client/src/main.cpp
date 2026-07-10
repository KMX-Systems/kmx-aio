#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/someip/client.hpp>

#include <chrono>
#include <iostream>
#include <memory>
#include <source_location>
#include <vector>

#include <kmx/logger.hpp>

namespace detail
{
    kmx::aio::task<void> run_client(kmx::aio::someip::client& cli, std::shared_ptr<kmx::aio::completion::executor> exec, int& exit_code)
    {
        std::cout << "SOMEIP_ECHO_CLIENT_START" << std::endl;

        const auto start_result = co_await cli.start();
        if (!start_result)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SOME/IP client start failed: {}",
                             start_result.error().message());
            exit_code = 1;
            exec->stop();
            co_return;
        }

        const auto request_result = co_await cli.request_service(0x1111u, 0x2222u);
        if (!request_result)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SOME/IP request_service failed: {}",
                             request_result.error().message());
            exit_code = 1;
            exec->stop();
            co_return;
        }

        const std::vector<std::uint8_t> payload = {'p', 'i', 'n', 'g'};
        const auto call_result = co_await cli.call_method(0x1111u, 0x2222u, 0x3333u, payload);
        if (!call_result)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SOME/IP call_method failed: {}",
                             call_result.error().message());
            exit_code = 1;
            exec->stop();
            co_return;
        }

        const auto stop_result = co_await cli.stop();
        if (!stop_result)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SOME/IP client stop failed: {}",
                             stop_result.error().message());
            exit_code = 1;
            exec->stop();
            co_return;
        }

        std::cout << "SOMEIP_ECHO_CLIENT_DONE" << std::endl;
        exit_code = 0;
        exec->stop();
    }
}

int main() noexcept
{
    try
    {
        auto exec = std::make_shared<kmx::aio::completion::executor>();
        kmx::aio::someip::client cli {{
            .application_name = "kmx_someip_echo_client",
            .config_file_path = "",
            .service_id = 0x1111u,
            .instance_id = 0x2222u,
            .connect_timeout = std::chrono::milliseconds(500),
            .iterate_timeout = std::chrono::milliseconds(10),
        }};

        int exit_code = 1;
        exec->spawn(detail::run_client(cli, exec, exit_code));
        exec->run();
        return exit_code;
    }
    catch (const std::exception& e)
    {
        kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Fatal error: {}", e.what());
        return 1;
    }
}
