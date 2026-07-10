/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/someip/client.hpp>
#include <kmx/aio/someip/vsomeip_compat.hpp>

#include <optional>
#include <unordered_set>
#include <utility>

namespace kmx::aio::someip
{
    namespace
    {
        [[nodiscard]] std::uint32_t service_key(const service_id_t service_id, const instance_id_t instance_id) noexcept
        {
            return (static_cast<std::uint32_t>(service_id) << 16u) | static_cast<std::uint32_t>(instance_id);
        }
    } // anonymous namespace

    struct client::impl
    {
        explicit impl(client_config cfg) noexcept: config(std::move(cfg)), runtime(config.application_name, config.config_file_path) {}

        client_config config;
        compat::client_runtime runtime;
        mutable statistics stats;
        bool started = false;
        std::unordered_set<std::uint32_t> available_services;
#if !defined(KMX_AIO_HAS_VSOMEIP_HEADER)
        std::optional<std::uint32_t> next_call_status;
#endif
    };

    client::client(client_config config) noexcept: impl_(std::make_unique<impl>(std::move(config)))
    {
    }
    client::~client() noexcept = default;
    client::client(client&&) noexcept = default;
    client& client::operator=(client&&) noexcept = default;

    task<std::expected<void, std::error_code>> client::start() noexcept(false)
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

    task<std::expected<void, std::error_code>> client::stop() noexcept(false)
    {
        if (!impl_->started)
            co_return std::unexpected(make_error_code(error::stopped));

        (void) impl_->runtime.stop();
        impl_->started = false;
        impl_->available_services.clear();
        co_return std::expected<void, std::error_code> {};
    }

    task<std::expected<bool, std::error_code>> client::iterate(const std::chrono::milliseconds timeout) noexcept(false)
    {
        if (timeout.count() < 0)
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        if (!impl_->started)
            co_return false;

        co_return true;
    }

    task<std::expected<void, std::error_code>> client::request_service(const service_id_t service_id,
                                                                       const instance_id_t instance_id) noexcept(false)
    {
        if (!impl_->started)
            co_return std::unexpected(make_error_code(error::not_initialized));

        if (!impl_->runtime.request_service(service_id, instance_id))
            co_return std::unexpected(make_error_code(error::request_failed));

        impl_->available_services.insert(service_key(service_id, instance_id));
        co_return std::expected<void, std::error_code> {};
    }

    task<std::expected<void, std::error_code>> client::release_service(const service_id_t service_id,
                                                                       const instance_id_t instance_id) noexcept(false)
    {
        if (!impl_->started)
            co_return std::unexpected(make_error_code(error::not_initialized));

        if (!impl_->runtime.release_service(service_id, instance_id))
            co_return std::unexpected(make_error_code(error::request_failed));

        impl_->available_services.erase(service_key(service_id, instance_id));
        co_return std::expected<void, std::error_code> {};
    }

    task<std::expected<call_result, std::error_code>> client::call_method(const service_id_t service_id, const instance_id_t instance_id,
                                                                          const method_id_t method_id,
                                                                          std::vector<std::uint8_t> payload) noexcept(false)
    {
        if (!impl_->started)
            co_return std::unexpected(make_error_code(error::not_initialized));

        const std::uint32_t key = service_key(service_id, instance_id);
        if (!impl_->available_services.contains(key))
            co_return std::unexpected(make_error_code(error::service_unavailable));

        ++impl_->stats.call_requests;
        ++impl_->stats.calls_sent;

#if !defined(KMX_AIO_HAS_VSOMEIP_HEADER)
        if (impl_->next_call_status.has_value())
        {
            const std::uint32_t status = *impl_->next_call_status;
            impl_->next_call_status.reset();
            if (status != 0u)
                co_return std::unexpected(make_error_code(error::request_failed));
        }
#endif

        const auto response =
            impl_->runtime.call_method(service_id, instance_id, method_id, std::move(payload), impl_->config.connect_timeout);
        if (!response.has_value())
            co_return std::unexpected(make_error_code(error::timed_out));

        ++impl_->stats.calls_received;
        co_return call_result {
            .service_id = response->service_id,
            .instance_id = response->instance_id,
            .method_id = response->method_id,
            .payload = std::move(response->payload),
        };
    }

    bool client::is_service_available(const service_id_t service_id, const instance_id_t instance_id) const noexcept
    {
        if (!impl_->started)
            return false;

        return impl_->runtime.is_service_available(service_id, instance_id) ||
               impl_->available_services.contains(service_key(service_id, instance_id));
    }

    const client_config& client::config() const noexcept
    {
        return impl_->config;
    }

    const statistics& client::get_stats() const noexcept
    {
        impl_->stats.dropped_events = impl_->runtime.dropped_events();
        return impl_->stats;
    }

#if !defined(KMX_AIO_HAS_VSOMEIP_HEADER)
    void client::__kmx_test_inject_service_available(const service_id_t service_id, const instance_id_t instance_id) noexcept
    {
        impl_->available_services.insert(service_key(service_id, instance_id));
    }

    void client::__kmx_test_set_next_call_status(const std::uint32_t status) noexcept
    {
        impl_->next_call_status = status;
    }
#endif

} // namespace kmx::aio::someip
