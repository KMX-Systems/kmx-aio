/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/someip/client.hpp>
#include <kmx/aio/someip/error.hpp>
#include <kmx/aio/someip/subscription.hpp>
#include <kmx/aio/someip/vsomeip_compat.hpp>

#include <deque>
#include <optional>
#include <utility>

namespace kmx::aio::someip
{
    struct subscription::impl
    {
        explicit impl(subscription_config cfg) noexcept: config(std::move(cfg)) {}

        subscription_config config;
        client* bound_client = nullptr;
        bool opened = false;
        std::deque<event_notification> queue;
        std::optional<compat::client_runtime> runtime;
        std::uint64_t local_dropped_events = 0u;
    };

    subscription::subscription(subscription_config config) noexcept: impl_(std::make_unique<impl>(std::move(config)))
    {
    }

    subscription::subscription(client& bound_client, subscription_config config) noexcept: impl_(std::make_unique<impl>(std::move(config)))
    {
        impl_->bound_client = &bound_client;
    }

    subscription::~subscription() noexcept = default;
    subscription::subscription(subscription&&) noexcept = default;
    subscription& subscription::operator=(subscription&&) noexcept = default;

    task<std::expected<void, std::error_code>> subscription::open() noexcept(false)
    {
        if (impl_->bound_client == nullptr)
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        if (impl_->opened)
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        const auto& client_cfg = impl_->bound_client->config();
        std::string app_name = client_cfg.application_name;
        if (app_name.empty())
            app_name = "kmx_someip_subscription_client";

        app_name += "_sub";
        impl_->runtime.emplace(std::move(app_name), client_cfg.config_file_path);

        if (!impl_->runtime->start())
            co_return std::unexpected(make_error_code(error::start_failed));

        if (!impl_->runtime->request_service(impl_->config.service_id, impl_->config.instance_id))
        {
            (void) impl_->runtime->stop();
            impl_->runtime.reset();
            co_return std::unexpected(make_error_code(error::request_failed));
        }

        if (!impl_->runtime->subscribe(impl_->config.service_id, impl_->config.instance_id, impl_->config.event_group_id,
                                       impl_->config.event_ids, impl_->config.notification_queue_capacity))
        {
            (void) impl_->runtime->stop();
            impl_->runtime.reset();
            co_return std::unexpected(make_error_code(error::request_failed));
        }

        impl_->opened = true;
        co_return std::expected<void, std::error_code> {};
    }

    task<std::expected<void, std::error_code>> subscription::close() noexcept(false)
    {
        if (!impl_->opened)
            co_return std::unexpected(make_error_code(error::subscription_closed));

        if (impl_->runtime.has_value())
        {
            (void) impl_->runtime->unsubscribe(impl_->config.service_id, impl_->config.instance_id, impl_->config.event_group_id,
                                               impl_->config.event_ids);
            (void) impl_->runtime->release_service(impl_->config.service_id, impl_->config.instance_id);
            (void) impl_->runtime->stop();
            impl_->runtime.reset();
        }

        impl_->opened = false;
        impl_->queue.clear();
        co_return std::expected<void, std::error_code> {};
    }

    task<std::expected<event_notification, std::error_code>> subscription::next() noexcept(false)
    {
        if (!impl_->opened)
            co_return std::unexpected(make_error_code(error::subscription_closed));

        if (impl_->queue.empty())
        {
            if (!impl_->runtime.has_value())
                co_return std::unexpected(make_error_code(error::timed_out));

            const auto enqueue_notification = [this](event_notification&& notification)
            {
                const std::size_t queue_capacity = impl_->config.notification_queue_capacity;
                if (queue_capacity == 0u)
                {
                    ++impl_->local_dropped_events;
                    return;
                }

                while (impl_->queue.size() >= queue_capacity)
                {
                    impl_->queue.pop_front();
                    ++impl_->local_dropped_events;
                }

                impl_->queue.push_back(std::move(notification));
            };

            auto next_event = impl_->runtime->next_event(impl_->config.iterate_timeout);
            if (!next_event.has_value())
                co_return std::unexpected(make_error_code(error::timed_out));

            enqueue_notification(std::move(next_event.value()));

            while (true)
            {
                auto extra_event = impl_->runtime->next_event(std::chrono::milliseconds {});
                if (!extra_event.has_value())
                    break;

                enqueue_notification(std::move(extra_event.value()));
            }

            if (impl_->queue.empty())
                co_return std::unexpected(make_error_code(error::timed_out));
        }

        event_notification notification = std::move(impl_->queue.front());
        impl_->queue.pop_front();
        co_return notification;
    }

    std::expected<void, std::error_code> subscription::bind(client& bound_client) noexcept
    {
        if (impl_->opened)
            return std::unexpected(make_error_code(error::invalid_configuration));

        impl_->bound_client = &bound_client;
        return {};
    }

    const subscription_config& subscription::config() const noexcept
    {
        return impl_->config;
    }

    std::uint64_t subscription::dropped_events() const noexcept
    {
        if (!impl_->runtime.has_value())
            return impl_->local_dropped_events;

        return impl_->local_dropped_events + impl_->runtime->dropped_events();
    }

#if !defined(KMX_AIO_HAS_VSOMEIP_HEADER)
    void subscription::__kmx_test_push_event(event_notification notification)
    {
        if (!impl_->runtime.has_value())
            return;

        impl_->runtime->__kmx_test_push_event(std::move(notification));
    }
#endif

} // namespace kmx::aio::someip
