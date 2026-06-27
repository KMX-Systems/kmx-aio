/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/opc_ua/error.hpp>
#include <kmx/aio/opc_ua/client.hpp>
#include <kmx/aio/opc_ua/subscription.hpp>

#include <kmx/aio/channel.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace kmx::aio::opc_ua
{
    namespace
    {
        [[nodiscard]] bool is_valid(const subscription_config& config) noexcept
        {
            return config.publishing_interval_ms > 0.0
                && config.lifetime_count > 0u
                && config.max_keepalive_count > 0u
                && config.max_keepalive_count <= config.lifetime_count
                && config.max_notifications_per_publish > 0u
                && config.notification_queue_capacity >= 2u;
        }
    }

    struct subscription::impl
    {
        explicit impl(subscription_config cfg) noexcept:
            config(std::move(cfg)),
            notifications(config.notification_queue_capacity)
        {
        }

        subscription_config config;
        channel<notification> notifications;
        client* bound_client = nullptr;
        bool opened = false;
        std::uint64_t heartbeat_sequence = 0u;
        std::chrono::steady_clock::time_point next_heartbeat_due {};
    };

    subscription::subscription(subscription_config config) noexcept:
        impl_(std::make_unique<impl>(std::move(config)))
    {
    }

    subscription::subscription(client& bound_client, subscription_config config) noexcept:
        impl_(std::make_unique<impl>(std::move(config)))
    {
        impl_->bound_client = &bound_client;
    }

    subscription::~subscription() noexcept = default;
    subscription::subscription(subscription&&) noexcept = default;
    subscription& subscription::operator=(subscription&&) noexcept = default;

    task<std::expected<void, std::error_code>> subscription::open() noexcept(false)
    {
        if (!is_valid(impl_->config))
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        if (impl_->bound_client == nullptr)
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        if (!impl_->bound_client->has_active_session())
            co_return std::unexpected(make_error_code(error::not_initialized));

        if (impl_->opened)
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        impl_->opened = true;
        impl_->heartbeat_sequence = 0u;
        impl_->next_heartbeat_due = std::chrono::steady_clock::now();
        co_return std::expected<void, std::error_code> {};
    }

    task<std::expected<void, std::error_code>> subscription::close() noexcept(false)
    {
        if (!impl_->opened)
            co_return std::unexpected(make_error_code(error::subscription_closed));

        impl_->opened = false;
        co_return std::expected<void, std::error_code> {};
    }

    task<std::expected<notification, std::error_code>> subscription::next() noexcept(false)
    {
        if (!impl_->opened)
            co_return std::unexpected(make_error_code(error::subscription_closed));

        if (auto queued = impl_->notifications.try_pop(); queued.has_value())
            co_return std::move(*queued);

        const auto now = std::chrono::steady_clock::now();
        if (now < impl_->next_heartbeat_due)
            co_return std::unexpected(make_error_code(error::timed_out));

        if (!impl_->config.emit_heartbeat_when_idle)
            co_return std::unexpected(make_error_code(error::timed_out));

        notification n;
        n.node_id = impl_->config.monitored_node_ids.empty() ? "opcua://subscription/heartbeat" : impl_->config.monitored_node_ids.front();
        n.payload = std::string {"{\"sequence\":"} + std::to_string(impl_->heartbeat_sequence) + "}";
        n.source_timestamp = std::chrono::system_clock::now();

        ++impl_->heartbeat_sequence;
        const auto enqueued = impl_->notifications.try_push(std::move(n));
        const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double, std::milli>(impl_->config.publishing_interval_ms));
        impl_->next_heartbeat_due = now + period;

        if (!enqueued)
            co_return std::unexpected(make_error_code(error::request_failed));

        auto queued = impl_->notifications.try_pop();
        if (!queued.has_value())
            co_return std::unexpected(make_error_code(error::internal_error));

        co_return std::move(*queued);
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

} // namespace kmx::aio::opc_ua
