#include <kmx/aio/sample/someip/echo_server/manager.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <source_location>
#include <utility>
#include <vector>

#include <kmx/aio/someip/error.hpp>
#include <kmx/logger.hpp>

namespace kmx::aio::sample::someip::echo_server
{
    manager::manager(kmx::aio::someip::server_config config) noexcept
        : server_(std::move(config))
    {
    }

    kmx::aio::task<void> manager::run(kmx::aio::completion::executor& exec,
                                      std::shared_ptr<std::atomic_bool> ok) noexcept(false)
    {
        const auto& cfg = server_.config();

        const auto start_result = co_await server_.start();
        if (!start_result)
        {
            kmx::logger::log(kmx::logger::level::error,
                             std::source_location::current(),
                             "SOME/IP server start failed: {}",
                             start_result.error().message());
            exec.stop();
            co_return;
        }

        const auto offer_result = co_await server_.offer_service(cfg.service_id, cfg.instance_id);
        if (!offer_result)
        {
            kmx::logger::log(kmx::logger::level::error,
                             std::source_location::current(),
                             "SOME/IP offer_service failed: {}",
                             offer_result.error().message());
            (void) co_await server_.stop();
            exec.stop();
            co_return;
        }

        kmx::logger::log(kmx::logger::level::info,
                         std::source_location::current(),
                         "SOME/IP echo server cfg service=0x{:04x} instance=0x{:04x}",
                         static_cast<std::uint32_t>(cfg.service_id),
                         static_cast<std::uint32_t>(cfg.instance_id));
        std::cout << "SOMEIP_ECHO_SERVER_START" << std::endl;

        bool replied = false;
        constexpr auto loop_wait = std::chrono::milliseconds(10);

        for (int i = 0; i < 800 && !replied; ++i)
        {
            auto request = co_await server_.next_request();
            if (!request)
            {
                if (request.error() == kmx::aio::someip::error::timed_out)
                {
                    auto tick = co_await server_.iterate(loop_wait);
                    if (!tick)
                    {
                        kmx::logger::log(kmx::logger::level::error,
                                         std::source_location::current(),
                                         "SOME/IP iterate failed: {}",
                                         tick.error().message());
                        break;
                    }
                    continue;
                }

                kmx::logger::log(kmx::logger::level::error,
                                 std::source_location::current(),
                                 "SOME/IP next_request failed: {}",
                                 request.error().message());
                break;
            }

            auto response = co_await server_.send_response(request->request_id, std::move(request->payload));
            if (!response)
            {
                kmx::logger::log(kmx::logger::level::error,
                                 std::source_location::current(),
                                 "SOME/IP send_response failed: {}",
                                 response.error().message());
                break;
            }

            replied = true;
        }

        const auto stop_offer_result = co_await server_.stop_offer_service(cfg.service_id, cfg.instance_id);
        if (!stop_offer_result)
            kmx::logger::log(kmx::logger::level::warn,
                             std::source_location::current(),
                             "SOME/IP stop_offer_service failed: {}",
                             stop_offer_result.error().message());

        const auto stop_result = co_await server_.stop();
        if (!stop_result)
            kmx::logger::log(kmx::logger::level::warn,
                             std::source_location::current(),
                             "SOME/IP server stop failed: {}",
                             stop_result.error().message());

        const auto& stats = server_.get_stats();
        kmx::logger::log(kmx::logger::level::info,
                         std::source_location::current(),
                         "SOME/IP echo server stats calls_received={} calls_sent={} events_sent={}",
                         stats.calls_received,
                         stats.calls_sent,
                         stats.events_sent);

        std::cout << "SOMEIP_ECHO_SERVER_STOP" << std::endl;
        ok->store(replied, std::memory_order_relaxed);
        exec.stop();
    }
}
