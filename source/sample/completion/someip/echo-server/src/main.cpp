#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/someip/server.hpp>

#include <chrono>
#include <iostream>
#include <memory>
#include <source_location>

#include <kmx/logger.hpp>

namespace detail
{
    kmx::aio::task<void> run_server(kmx::aio::someip::server& srv, std::shared_ptr<kmx::aio::completion::executor> exec, int& exit_code)
    {
        const auto start_result = co_await srv.start();
        if (!start_result)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SOME/IP server start failed: {}",
                             start_result.error().message());
            exit_code = 1;
            exec->stop();
            co_return;
        }

        const auto offer_result = co_await srv.offer_service(0x1111u, 0x2222u);
        if (!offer_result)
        {
            kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "SOME/IP offer_service failed: {}",
                             offer_result.error().message());
            exit_code = 1;
            exec->stop();
            co_return;
        }

        std::cout << "SOMEIP_ECHO_SERVER_START" << std::endl;

        for (int i = 0; i < 200; ++i)
            (void) co_await srv.iterate(std::chrono::milliseconds(10));

        (void) co_await srv.stop_offer_service(0x1111u, 0x2222u);
        (void) co_await srv.stop();

        std::cout << "SOMEIP_ECHO_SERVER_STOP" << std::endl;
        exit_code = 0;
        exec->stop();
    }
}

int main() noexcept
{
    try
    {
        auto exec = std::make_shared<kmx::aio::completion::executor>();
        kmx::aio::someip::server srv {{
            .application_name = "kmx_someip_echo_server",
            .config_file_path = "",
            .service_id = 0x1111u,
            .instance_id = 0x2222u,
            .iterate_timeout = std::chrono::milliseconds(10),
        }};

        int exit_code = 1;
        exec->spawn(detail::run_server(srv, exec, exit_code));
        exec->run();
        return exit_code;
    }
    catch (const std::exception& e)
    {
        kmx::logger::log(kmx::logger::level::error, std::source_location::current(), "Fatal error: {}", e.what());
        return 1;
    }
}
