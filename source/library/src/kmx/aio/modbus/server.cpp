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
    using async_result = task_returning_expected_void_t;

    struct server::impl : detail::server_ops<server::impl>
    {
        std::unordered_map<std::uint8_t, request_handler> handlers_;
        std::stop_source stop_source_;

        [[nodiscard]] task<void>
        handle_connection(readiness::executor& exec, file_descriptor fd,
                          const server_config& config) noexcept(false)
        {
            const auto connection_fd = fd.get();
            readiness::tcp::stream stream {exec, std::move(fd)};
            const auto stop_token = stop_source_.get_token();

            // The read this connection parks in between requests has no timeout, so an idle peer would
            // otherwise keep the task - and the executor's run() - alive for as long as it stays
            // connected. stop() has to reach here as well as the accept loop.
            const std::stop_callback cancel_on_stop {stop_token, [&exec, connection_fd]() noexcept
                                                     { exec.cancel_io(connection_fd); }};

            while (!stop_token.stop_requested())
            {
                if (!co_await process_request(stream, config))
                    break;
            }
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
        ipv4::storage_t bind_ip = ipv4::any;
        if (!config.bind_address.empty())
        {
            bind_ip = ipv4::storage_t {};
            if (!ipv4::parse_address(config.bind_address, bind_ip))
                co_return std::unexpected(make_error_code(error::invalid_configuration));
        }

        readiness::tcp::listener listener {exec, ipv4::make_address(bind_ip), config.port};
        if (const auto r = listener.listen(); !r)
            co_return std::unexpected(r.error());

        const auto stop_token = impl_->stop_source_.get_token();

        // stop() only sets a flag that the loop below reads between connections. The accept it is
        // suspended in has to be woken too, or serve() stays outstanding for good - and an executor
        // whose run() returns once its work drains then never returns at all.
        const std::stop_callback cancel_on_stop {stop_token, [&exec, fd = listener.get_fd()]() noexcept
                                                 { exec.cancel_io(fd); }};

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

        co_return expected_void_t();
    }

    void server::stop() noexcept
    {
        impl_->stop_source_.request_stop();
    }

} // namespace kmx::aio::modbus
#endif // KMX_AIO_FEATURE_MODBUS
