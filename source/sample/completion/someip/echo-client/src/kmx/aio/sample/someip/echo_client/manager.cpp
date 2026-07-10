#include <kmx/aio/sample/someip/echo_client/manager.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <source_location>
#include <utility>
#include <vector>

#include <kmx/logger.hpp>

namespace kmx::aio::sample::someip::echo_client
{
    manager::manager(kmx::aio::someip::client_config config) noexcept
        : client_(std::move(config))
    {
    }

    kmx::aio::task<void> manager::run(std::shared_ptr<kmx::aio::completion::executor> exec,
                                      std::shared_ptr<std::atomic_bool> ok) noexcept(false)
    {
        const auto& cfg = client_.config();

        std::cout << "SOMEIP_ECHO_CLIENT_START" << std::endl;
        kmx::logger::log(kmx::logger::level::info,
                         std::source_location::current(),
                         "SOME/IP echo client cfg service=0x{:04x} instance=0x{:04x}",
                         static_cast<std::uint32_t>(cfg.service_id),
                         static_cast<std::uint32_t>(cfg.instance_id));

        const auto start_result = co_await client_.start();
        if (!start_result)
        {
            kmx::logger::log(kmx::logger::level::error,
                             std::source_location::current(),
                             "SOME/IP client start failed: {}",
                             start_result.error().message());
            exec->stop();
            co_return;
        }

        const auto request_result = co_await client_.request_service(cfg.service_id, cfg.instance_id);
        if (!request_result)
        {
            kmx::logger::log(kmx::logger::level::error,
                             std::source_location::current(),
                             "SOME/IP request_service failed: {}",
                             request_result.error().message());
            (void) co_await client_.stop();
            exec->stop();
            co_return;
        }

        bool available = false;
        for (int i = 0; i < 200; ++i)
        {
            if (client_.is_service_available(cfg.service_id, cfg.instance_id))
            {
                available = true;
                break;
            }

            auto tick = co_await client_.iterate(std::chrono::milliseconds(10));
            if (!tick)
            {
                kmx::logger::log(kmx::logger::level::error,
                                 std::source_location::current(),
                                 "SOME/IP client iterate failed: {}",
                                 tick.error().message());
                break;
            }
        }

        if (!available)
        {
            kmx::logger::log(kmx::logger::level::error,
                             std::source_location::current(),
                             "SOME/IP service did not become available");
            (void) co_await client_.release_service(cfg.service_id, cfg.instance_id);
            (void) co_await client_.stop();
            exec->stop();
            co_return;
        }

        const std::vector<std::uint8_t> payload = {'p', 'i', 'n', 'g'};
        const auto call_result = co_await client_.call_method(cfg.service_id, cfg.instance_id, 0x3333u, payload);
        if (!call_result)
        {
            kmx::logger::log(kmx::logger::level::error,
                             std::source_location::current(),
                             "SOME/IP call_method failed: {}",
                             call_result.error().message());
            (void) co_await client_.release_service(cfg.service_id, cfg.instance_id);
            (void) co_await client_.stop();
            exec->stop();
            co_return;
        }

        if (call_result->payload != payload)
        {
            kmx::logger::log(kmx::logger::level::error,
                             std::source_location::current(),
                             "SOME/IP payload mismatch in echo response");
            (void) co_await client_.release_service(cfg.service_id, cfg.instance_id);
            (void) co_await client_.stop();
            exec->stop();
            co_return;
        }

        const auto release_result = co_await client_.release_service(cfg.service_id, cfg.instance_id);
        if (!release_result)
            kmx::logger::log(kmx::logger::level::warn,
                             std::source_location::current(),
                             "SOME/IP release_service failed: {}",
                             release_result.error().message());

        const auto stop_result = co_await client_.stop();
        if (!stop_result)
        {
            kmx::logger::log(kmx::logger::level::error,
                             std::source_location::current(),
                             "SOME/IP client stop failed: {}",
                             stop_result.error().message());
            exec->stop();
            co_return;
        }

        const auto& stats = client_.get_stats();
        kmx::logger::log(kmx::logger::level::info,
                         std::source_location::current(),
                         "SOME/IP echo client stats calls_sent={} calls_received={} dropped_events={}",
                         stats.calls_sent,
                         stats.calls_received,
                         stats.dropped_events);

        std::cout << "SOMEIP_ECHO_CLIENT_DONE" << std::endl;
        ok->store(true, std::memory_order_relaxed);
        exec->stop();
    }
}
