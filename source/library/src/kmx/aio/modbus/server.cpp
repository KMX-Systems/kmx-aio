/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/modbus/server.hpp>
#if defined(KMX_AIO_FEATURE_MODBUS)
    #include <kmx/aio/modbus/detail/server_ops.hpp>
    #include <kmx/aio/modbus/detail/session.hpp>
    #include <kmx/aio/modbus/error.hpp>
    #include <kmx/aio/modbus/frame.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/readiness/tcp/listener.hpp>
    #include <kmx/aio/readiness/tcp/stream.hpp>

    #include <atomic>
    #include <cstdint>
    #include <stop_token>
    #include <unordered_map>
    #include <utility>

namespace kmx::aio::modbus
{
    // Type alias for common async result type
    using async_result = task<std::expected<void, std::error_code>>;

    struct server::impl : detail::server_ops<server::impl>
    {
        std::unordered_map<std::uint8_t, request_handler> handlers_;
        std::stop_source stop_source_;

        [[nodiscard]] task<void>
        handle_connection(readiness::executor& exec, file_descriptor fd,
                          const server_config& config) noexcept(false)
        {
            readiness::tcp::stream stream {exec, std::move(fd)};
            const auto stop_token = stop_source_.get_token();

            while (!stop_token.stop_requested())
                co_await process_request(stream, config);
        }
    };

    server::server() noexcept: impl_(std::make_unique<impl>()) {}

    server::~server() noexcept = default;
    server::server(server&&) noexcept = default;
    server& server::operator=(server&&) noexcept = default;

    void server::set_handler(const function_code fc, request_handler handler)
    {
        impl_->handlers_[static_cast<std::uint8_t>(fc)] = std::move(handler);
    }

    async_result
    server::serve(readiness::executor& exec, server_config config) noexcept(false)
    {
        ipv4_storage_t bind_ip = any_ipv4;
        if (!config.bind_address.empty())
        {
            bind_ip = ipv4_storage_t {};
            if (!parse_ipv4_address(config.bind_address, bind_ip))
                co_return std::unexpected(make_error_code(error::invalid_configuration));
        }

        readiness::tcp::listener listener {exec, make_ipv4_address(bind_ip), config.port};
        if (const auto r = listener.listen(); !r)
            co_return std::unexpected(r.error());

        const auto stop_token = impl_->stop_source_.get_token();

        while (!stop_token.stop_requested())
        {
            auto fd_result = co_await listener.accept();
            if (!fd_result)
            {
                if (stop_token.stop_requested())
                    break;
                co_return std::unexpected(fd_result.error());
            }

            exec.spawn(
                impl_->handle_connection(exec, std::move(*fd_result), config));
        }

        co_return std::expected<void, std::error_code>();
    }

    void server::stop() noexcept
    {
        impl_->stop_source_.request_stop();
    }

} // namespace kmx::aio::modbus
#endif // KMX_AIO_FEATURE_MODBUS
