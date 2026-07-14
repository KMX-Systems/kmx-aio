#include <kmx/aio/sample/someip/event_publisher/manager.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <source_location>
#include <utility>
#include <vector>

#include <kmx/logger.hpp>

namespace kmx::aio::sample::someip::event_publisher
{
    manager::manager(kmx::aio::someip::server_config config,
                     kmx::aio::someip::event_id_t event_id,
                     std::size_t event_count) noexcept
        : server_(std::move(config))
        , event_id_(event_id)
        , event_count_(event_count)
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
                             "SOME/IP publisher start failed: {}",
                             start_result.error().message());
            exec.stop();
            co_return;
        }

        const auto offer_result = co_await server_.offer_service(cfg.service_id, cfg.instance_id);
        if (!offer_result)
        {
            kmx::logger::log(kmx::logger::level::error,
                             std::source_location::current(),
                             "SOME/IP publisher offer_service failed: {}",
                             offer_result.error().message());
            (void) co_await server_.stop();
            exec.stop();
            co_return;
        }

        std::cout << "SOMEIP_EVENT_PUBLISHER_START" << std::endl;

        for (int warmup = 0; warmup < 150; ++warmup)
        {
            auto tick = co_await server_.iterate(std::chrono::milliseconds(10));
            if (!tick)
            {
                kmx::logger::log(kmx::logger::level::warn,
                                 std::source_location::current(),
                                 "SOME/IP publisher warmup iterate failed: {}",
                                 tick.error().message());
                break;
            }
        }

        bool all_sent = true;
        for (std::size_t i = 0; i < event_count_; ++i)
        {
            std::vector<std::uint8_t> payload {
                static_cast<std::uint8_t>('e'),
                static_cast<std::uint8_t>('v'),
                static_cast<std::uint8_t>('t'),
                static_cast<std::uint8_t>('0' + (i % 10u)),
            };

            const auto notify_result = co_await server_.notify(cfg.service_id, cfg.instance_id, event_id_, std::move(payload));
            if (!notify_result)
            {
                kmx::logger::log(kmx::logger::level::error,
                                 std::source_location::current(),
                                 "SOME/IP publisher notify failed: {}",
                                 notify_result.error().message());
                all_sent = false;
                break;
            }

            auto tick = co_await server_.iterate(std::chrono::milliseconds(20));
            if (!tick)
            {
                kmx::logger::log(kmx::logger::level::warn,
                                 std::source_location::current(),
                                 "SOME/IP publisher iterate failed: {}",
                                 tick.error().message());
                all_sent = false;
                break;
            }
        }

        const auto stop_offer_result = co_await server_.stop_offer_service(cfg.service_id, cfg.instance_id);
        if (!stop_offer_result)
            kmx::logger::log(kmx::logger::level::warn,
                             std::source_location::current(),
                             "SOME/IP publisher stop_offer_service failed: {}",
                             stop_offer_result.error().message());

        const auto stop_result = co_await server_.stop();
        if (!stop_result)
            kmx::logger::log(kmx::logger::level::warn,
                             std::source_location::current(),
                             "SOME/IP publisher stop failed: {}",
                             stop_result.error().message());

        const auto& stats = server_.get_stats();
        kmx::logger::log(kmx::logger::level::info,
                         std::source_location::current(),
                         "SOME/IP publisher stats events_sent={} calls_received={}",
                         stats.events_sent,
                         stats.calls_received);

        std::cout << "SOMEIP_EVENT_PUBLISHER_STOP" << std::endl;
        ok->store(all_sent, std::memory_order_relaxed);
        exec.stop();
    }
}
