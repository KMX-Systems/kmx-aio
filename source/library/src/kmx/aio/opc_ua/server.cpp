/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/opc_ua/server.hpp>
#include <kmx/aio/opc_ua/open62541_compat.hpp>

#include <chrono>
#include <memory>
#include <utility>

namespace kmx::aio::opc_ua
{
    namespace
    {
        enum class lifecycle_state
        {
            idle,
            running,
            stopping,
        };

        [[nodiscard]] std::error_code map_status_to_error(const UA_StatusCode status) noexcept
        {
            switch (status)
            {
                case UA_STATUSCODE_GOOD:
                    return {};
                case UA_STATUSCODE_BADTIMEOUT:
                    return make_error_code(error::timed_out);
                case UA_STATUSCODE_BADCONFIGURATIONERROR:
                    return make_error_code(error::invalid_configuration);
                default:
                    return make_error_code(error::internal_error);
            }
        }
    }

    struct server::impl
    {
        explicit impl(server_config cfg) noexcept:
            config(std::move(cfg))
        {
        }

        server_config config;
        UA_Server* native_server = nullptr;
        lifecycle_state state = lifecycle_state::idle;
        statistics stats;

        ~impl() noexcept
        {
            if (native_server != nullptr)
                UA_Server_delete(native_server);
        }
    };

    server::server(server_config config) noexcept:
        impl_(std::make_unique<impl>(std::move(config)))
    {
    }

    server::~server() noexcept = default;
    server::server(server&&) noexcept = default;
    server& server::operator=(server&&) noexcept = default;

    task<std::expected<void, std::error_code>> server::start() noexcept(false)
    {
        if (impl_->config.port == 0u)
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        if (impl_->native_server != nullptr)
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        if (impl_->config.mode != security_mode::none
            && (impl_->config.certificate_path.empty() || impl_->config.private_key_path.empty()))
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        impl_->native_server = UA_Server_new();
        if (impl_->native_server == nullptr)
            co_return std::unexpected(make_error_code(error::internal_error));

        const UA_StatusCode status = UA_Server_run_startup(impl_->native_server);
        if (status != UA_STATUSCODE_GOOD)
        {
            UA_Server_delete(impl_->native_server);
            impl_->native_server = nullptr;
            co_return std::unexpected(map_status_to_error(status));
        }

        impl_->state = lifecycle_state::running;
        co_return std::expected<void, std::error_code> {};
    }

    task<std::expected<void, std::error_code>> server::stop() noexcept(false)
    {
        if (impl_->native_server == nullptr)
            co_return std::unexpected(make_error_code(error::not_initialized));

        impl_->state = lifecycle_state::stopping;

        const UA_StatusCode status = UA_Server_run_shutdown(impl_->native_server);
        if (status != UA_STATUSCODE_GOOD)
        {
            impl_->state = lifecycle_state::running;
            co_return std::unexpected(map_status_to_error(status));
        }

        UA_Server_delete(impl_->native_server);
        impl_->native_server = nullptr;
        impl_->state = lifecycle_state::idle;
        co_return std::expected<void, std::error_code> {};
    }

    task<std::expected<std::uint16_t, std::error_code>> server::iterate(const std::chrono::milliseconds timeout) noexcept(false)
    {
        if (impl_->native_server == nullptr)
            co_return std::unexpected(make_error_code(error::not_initialized));

        if (timeout.count() < 0)
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        const UA_UInt16 next_iterate_ms = UA_Server_run_iterate(impl_->native_server, timeout.count() > 0 ? true : false);
        co_return static_cast<std::uint16_t>(next_iterate_ms);
    }

    const server_config& server::config() const noexcept
    {
        return impl_->config;
    }

    const statistics& server::get_stats() const noexcept
    {
        return impl_->stats;
    }

} // namespace kmx::aio::opc_ua
