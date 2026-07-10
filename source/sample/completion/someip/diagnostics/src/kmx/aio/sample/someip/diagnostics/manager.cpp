#include <kmx/aio/sample/someip/diagnostics/manager.hpp>

#include <chrono>
#include <iostream>
#include <source_location>
#include <string>
#include <utility>
#include <vector>

#include <kmx/aio/someip/error.hpp>
#include <kmx/logger.hpp>

namespace kmx::aio::sample::someip::diagnostics
{
    manager::manager(kmx::aio::someip::client_config client_config,
                     kmx::aio::someip::subscription_config subscription_config) noexcept
        : client_(std::move(client_config))
        , subscription_(std::move(subscription_config))
    {
    }

    kmx::aio::task<void> manager::run(std::shared_ptr<kmx::aio::completion::executor> exec,
                                      std::shared_ptr<std::atomic_bool> ok) noexcept(false)
    {
        const auto& client_cfg = client_.config();

        auto client_start = co_await client_.start();
        if (!client_start)
        {
            kmx::logger::log(kmx::logger::level::error,
                             std::source_location::current(),
                             "SOME/IP diagnostics client start failed: {}",
                             client_start.error().message());
            exec->stop();
            co_return;
        }

        auto request = co_await client_.request_service(client_cfg.service_id, client_cfg.instance_id);
        if (!request)
        {
            kmx::logger::log(kmx::logger::level::error,
                             std::source_location::current(),
                             "SOME/IP diagnostics request_service failed: {}",
                             request.error().message());
            (void) co_await client_.stop();
            exec->stop();
            co_return;
        }

        auto bind = subscription_.bind(client_);
        if (!bind)
        {
            kmx::logger::log(kmx::logger::level::error,
                             std::source_location::current(),
                             "SOME/IP diagnostics bind failed: {}",
                             bind.error().message());
            (void) co_await client_.release_service(client_cfg.service_id, client_cfg.instance_id);
            (void) co_await client_.stop();
            exec->stop();
            co_return;
        }

        auto open = co_await subscription_.open();
        if (!open)
        {
            kmx::logger::log(kmx::logger::level::error,
                             std::source_location::current(),
                             "SOME/IP diagnostics subscription open failed: {}",
                             open.error().message());
            (void) co_await client_.release_service(client_cfg.service_id, client_cfg.instance_id);
            (void) co_await client_.stop();
            exec->stop();
            co_return;
        }

        std::vector<std::uint8_t> payload {
            static_cast<std::uint8_t>('d'),
            static_cast<std::uint8_t>('i'),
            static_cast<std::uint8_t>('a'),
            static_cast<std::uint8_t>('g'),
        };
        const auto call_result = co_await client_.call_method(
            client_cfg.service_id, client_cfg.instance_id, 0x3333u, std::move(payload));
        if (call_result)
            kmx::logger::log(kmx::logger::level::info,
                             std::source_location::current(),
                             "SOME/IP diagnostics call_method succeeded (response_size={})",
                             call_result->payload.size());
        else
            kmx::logger::log(kmx::logger::level::warn,
                             std::source_location::current(),
                             "SOME/IP diagnostics call_method: {} (expected without a real server)",
                             call_result.error().message());

        std::size_t received = 0;
        for (int i = 0; i < 30; ++i)
        {
            auto event = co_await subscription_.next();
            if (!event)
            {
                if (event.error() == kmx::aio::someip::error::timed_out)
                    continue;
                break;
            }
            ++received;
            const std::string payload_str(event->payload.begin(), event->payload.end());
            kmx::logger::log(kmx::logger::level::info,
                             std::source_location::current(),
                             "SOME/IP diagnostics received event #{} event_id=0x{:04x} payload='{}'",
                             received,
                             static_cast<std::uint32_t>(event->event_id),
                             payload_str);
            if (received >= 3u)
                break;
        }

        (void) co_await subscription_.close();
        (void) co_await client_.release_service(client_cfg.service_id, client_cfg.instance_id);
        (void) co_await client_.stop();

        const auto& client_stats = client_.get_stats();

        kmx::logger::log(kmx::logger::level::info,
                         std::source_location::current(),
                         "SOME/IP diagnostics client stats starts={} calls_sent={} calls_received={} dropped_events={}",
                         client_stats.start_attempts,
                         client_stats.calls_sent,
                         client_stats.calls_received,
                         client_stats.dropped_events);
        kmx::logger::log(kmx::logger::level::info,
                         std::source_location::current(),
                         "SOME/IP diagnostics subscription received={} dropped={}",
                         received,
                         subscription_.dropped_events());

        std::cout << "SOMEIP_DIAGNOSTICS_DONE" << std::endl;
        ok->store(true, std::memory_order_relaxed);
        exec->stop();
    }
}
