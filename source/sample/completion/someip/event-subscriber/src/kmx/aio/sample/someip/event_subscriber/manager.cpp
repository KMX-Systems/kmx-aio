#include <kmx/aio/sample/someip/event_subscriber/manager.hpp>

#include <chrono>
#include <iostream>
#include <source_location>
#include <string>
#include <utility>

#include <kmx/aio/someip/error.hpp>
#include <kmx/logger.hpp>

namespace kmx::aio::sample::someip::event_subscriber
{
    manager::manager(kmx::aio::someip::client_config client_config,
                     kmx::aio::someip::subscription_config subscription_config,
                     std::size_t expected_events) noexcept
        : client_(std::move(client_config))
        , subscription_(std::move(subscription_config))
        , expected_events_(expected_events)
    {
    }

    kmx::aio::task<void> manager::run(std::shared_ptr<kmx::aio::completion::executor> exec,
                                      std::shared_ptr<std::atomic_bool> ok) noexcept(false)
    {
        const auto& cfg = client_.config();
        std::cout << "SOMEIP_EVENT_SUBSCRIBER_START" << std::endl;

        auto start_result = co_await client_.start();
        if (!start_result)
        {
            kmx::logger::log(kmx::logger::level::error,
                             std::source_location::current(),
                             "SOME/IP subscriber client start failed: {}",
                             start_result.error().message());
            exec->stop();
            co_return;
        }

        auto request_result = co_await client_.request_service(cfg.service_id, cfg.instance_id);
        if (!request_result)
        {
            kmx::logger::log(kmx::logger::level::error,
                             std::source_location::current(),
                             "SOME/IP subscriber request_service failed: {}",
                             request_result.error().message());
            (void) co_await client_.stop();
            exec->stop();
            co_return;
        }

        auto bind_result = subscription_.bind(client_);
        if (!bind_result)
        {
            kmx::logger::log(kmx::logger::level::error,
                             std::source_location::current(),
                             "SOME/IP subscriber bind failed: {}",
                             bind_result.error().message());
            (void) co_await client_.release_service(cfg.service_id, cfg.instance_id);
            (void) co_await client_.stop();
            exec->stop();
            co_return;
        }

        auto open_result = co_await subscription_.open();
        if (!open_result)
        {
            kmx::logger::log(kmx::logger::level::error,
                             std::source_location::current(),
                             "SOME/IP subscriber open failed: {}",
                             open_result.error().message());
            (void) co_await client_.release_service(cfg.service_id, cfg.instance_id);
            (void) co_await client_.stop();
            exec->stop();
            co_return;
        }

        std::size_t received = 0;
        for (int i = 0; i < 500 && received < expected_events_; ++i)
        {
            auto notification = co_await subscription_.next();
            if (!notification)
            {
                if (notification.error() == kmx::aio::someip::error::timed_out)
                    continue;

                kmx::logger::log(kmx::logger::level::error,
                                 std::source_location::current(),
                                 "SOME/IP subscriber next failed: {}",
                                 notification.error().message());
                break;
            }

            ++received;
            const std::string payload_str(notification->payload.begin(), notification->payload.end());
            kmx::logger::log(kmx::logger::level::info,
                             std::source_location::current(),
                             "SOME/IP subscriber received event #{} event_id=0x{:04x} payload='{}'",
                             received,
                             static_cast<std::uint32_t>(notification->event_id),
                             payload_str);
        }

        auto close_result = co_await subscription_.close();
        if (!close_result)
            kmx::logger::log(kmx::logger::level::warn,
                             std::source_location::current(),
                             "SOME/IP subscriber close failed: {}",
                             close_result.error().message());

        auto release_result = co_await client_.release_service(cfg.service_id, cfg.instance_id);
        if (!release_result)
            kmx::logger::log(kmx::logger::level::warn,
                             std::source_location::current(),
                             "SOME/IP subscriber release_service failed: {}",
                             release_result.error().message());

        auto stop_result = co_await client_.stop();
        if (!stop_result)
            kmx::logger::log(kmx::logger::level::warn,
                             std::source_location::current(),
                             "SOME/IP subscriber stop failed: {}",
                             stop_result.error().message());

        const auto dropped = subscription_.dropped_events();
#if defined(KMX_AIO_SOMEIP_LINK_BACKEND)
        const bool success = (received >= expected_events_) && (dropped == 0u);
        if (!success)
            kmx::logger::log(kmx::logger::level::error,
                             std::source_location::current(),
                             "SOME/IP subscriber did not receive the expected number of events with the real backend");
#else
        if (received < expected_events_)
            kmx::logger::log(kmx::logger::level::warn,
                             std::source_location::current(),
                             "SOME/IP subscriber received fewer events than expected in stub mode; cross-process delivery is not available");

        const bool success = true;
#endif

        kmx::logger::log(kmx::logger::level::info,
                         std::source_location::current(),
                         "SOME/IP subscriber stats received={} dropped={} expected={}",
                         received,
                         dropped,
                         expected_events_);

        std::cout << "SOMEIP_EVENT_SUBSCRIBER_DONE" << std::endl;
        ok->store(success, std::memory_order_relaxed);
        exec->stop();
    }
}
