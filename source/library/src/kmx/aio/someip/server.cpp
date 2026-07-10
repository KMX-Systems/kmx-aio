/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/someip/server.hpp>
#include <kmx/aio/someip/vsomeip_compat.hpp>

#include <unordered_set>
#include <utility>

namespace kmx::aio::someip
{
    namespace detail
    {
        [[nodiscard]] std::uint32_t service_key(const service_id_t service_id, const instance_id_t instance_id) noexcept
        {
            return (static_cast<std::uint32_t>(service_id) << 16u) | static_cast<std::uint32_t>(instance_id);
        }
    } // namespace detail

    struct server::impl
    {
        explicit impl(server_config cfg) noexcept: config(std::move(cfg)), runtime(config.application_name, config.config_file_path) {}

        server_config config;
        compat::server_runtime runtime;
        statistics stats;
        bool started = false;
        std::unordered_set<std::uint32_t> offered_services;
    };

    server::server(server_config config) noexcept: impl_(std::make_unique<impl>(std::move(config)))
    {
    }
    server::~server() noexcept = default;
    server::server(server&&) noexcept = default;
    server& server::operator=(server&&) noexcept = default;

    task<std::expected<void, std::error_code>> server::start() noexcept(false)
    {
        if (impl_->config.application_name.empty())
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        ++impl_->stats.start_attempts;
        if (impl_->started)
            co_return std::unexpected(make_error_code(error::start_failed));

        if (!impl_->runtime.start())
            co_return std::unexpected(make_error_code(error::start_failed));

        impl_->started = true;
        ++impl_->stats.successful_starts;
        co_return std::expected<void, std::error_code> {};
    }

    task<std::expected<void, std::error_code>> server::stop() noexcept(false)
    {
        if (!impl_->started)
            co_return std::unexpected(make_error_code(error::stopped));

        (void) impl_->runtime.stop();
        impl_->started = false;
        impl_->offered_services.clear();
        co_return std::expected<void, std::error_code> {};
    }

    task<std::expected<bool, std::error_code>> server::iterate(const std::chrono::milliseconds timeout) noexcept(false)
    {
        if (timeout.count() < 0)
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        co_return impl_->started;
    }

    task<std::expected<void, std::error_code>> server::offer_service(const service_id_t service_id,
                                                                     const instance_id_t instance_id) noexcept(false)
    {
        if (!impl_->started)
            co_return std::unexpected(make_error_code(error::not_initialized));

        if (!impl_->runtime.offer_service(service_id, instance_id))
            co_return std::unexpected(make_error_code(error::request_failed));

        impl_->offered_services.insert(detail::service_key(service_id, instance_id));
        co_return std::expected<void, std::error_code> {};
    }

    task<std::expected<void, std::error_code>> server::stop_offer_service(const service_id_t service_id,
                                                                          const instance_id_t instance_id) noexcept(false)
    {
        if (!impl_->started)
            co_return std::unexpected(make_error_code(error::not_initialized));

        if (!impl_->runtime.stop_offer_service(service_id, instance_id))
            co_return std::unexpected(make_error_code(error::request_failed));

        impl_->offered_services.erase(detail::service_key(service_id, instance_id));
        co_return std::expected<void, std::error_code> {};
    }

    task<std::expected<method_request, std::error_code>> server::next_request() noexcept(false)
    {
        if (!impl_->started)
            co_return std::unexpected(make_error_code(error::not_initialized));

        if (const auto request_opt = impl_->runtime.next_request(); request_opt.has_value())
        {
            ++impl_->stats.calls_received;
            co_return method_request {
                .request_id = request_opt->request_id,
                .service_id = request_opt->service_id,
                .instance_id = request_opt->instance_id,
                .method_id = request_opt->method_id,
                .payload = std::move(request_opt->payload),
            };
        }

        co_return std::unexpected(make_error_code(error::timed_out));
    }

    task<std::expected<void, std::error_code>> server::send_response(const request_id_t request_id,
                                                                     std::vector<std::uint8_t> payload) noexcept(false)
    {
        if (!impl_->started)
            co_return std::unexpected(make_error_code(error::not_initialized));

        if (request_id == 0u)
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        if (!impl_->runtime.send_response(request_id, std::move(payload)))
            co_return std::unexpected(make_error_code(error::response_failed));

        ++impl_->stats.calls_sent;
        co_return std::expected<void, std::error_code> {};
    }

    task<std::expected<void, std::error_code>> server::notify(const service_id_t service_id, const instance_id_t instance_id,
                                                              const event_id_t event_id, std::vector<std::uint8_t> payload) noexcept(false)
    {
        if (!impl_->started)
            co_return std::unexpected(make_error_code(error::not_initialized));

        if (!impl_->offered_services.contains(detail::service_key(service_id, instance_id)))
            co_return std::unexpected(make_error_code(error::service_unavailable));

        if (!impl_->runtime.notify(service_id, instance_id, event_id, std::move(payload)))
            co_return std::unexpected(make_error_code(error::request_failed));

        ++impl_->stats.events_sent;
        co_return std::expected<void, std::error_code> {};
    }

    const server_config& server::config() const noexcept
    {
        return impl_->config;
    }

    const statistics& server::get_stats() const noexcept
    {
        return impl_->stats;
    }

} // namespace kmx::aio::someip
