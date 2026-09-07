/// @file aio/knx/gateway.hpp
/// @brief Composition wrapper for a KNX tunnelling server and routing client.
#pragma once
#ifndef PCH
    #include <expected>
    #include <system_error>
#endif

#include <kmx/aio/task.hpp>
#include <kmx/aio/knx/routing.hpp>
#include <kmx/aio/knx/server.hpp>

namespace kmx::aio::knx
{
    class gateway final
    {
    public:
        gateway(datagram_transport& transport,
                server_config server = {},
                routing::multicast_configuration routing = {}) noexcept:
            server_(transport, server), router_(transport, routing) {}

        [[nodiscard]] expected_void_t start() noexcept
        {
            const auto reset_result = server_.reset();
            if (!reset_result.has_value())
                return reset_result;
            return router_.start();
        }
        [[nodiscard]] expected_void_t stop() noexcept
        {
            const auto server_result = server_.shutdown();
            const auto router_result = router_.stop();
            if (!server_result.has_value())
                return server_result;
            return router_result;
        }

        [[nodiscard]] expected_void_t shutdown() noexcept { return stop(); }
        [[nodiscard]] task<std::expected<server_event, std::error_code>> serve_once() noexcept(false)
        {
            co_return co_await server_.serve_once();
        }
        [[nodiscard]] task_returning_expected_void_t serve() noexcept(false)
        {
            co_return co_await server_.serve();
        }

        [[nodiscard]] generic_server& server() noexcept { return server_; }
        [[nodiscard]] routing::client& router() noexcept { return router_; }

    private:
        generic_server server_;
        routing::client router_;
    };
}
