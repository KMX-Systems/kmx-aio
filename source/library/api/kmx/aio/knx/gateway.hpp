/// @file aio/knx/gateway.hpp
/// @brief Composition wrapper for a KNX tunnelling server and routing client.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
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

        [[nodiscard]] expected_void_t start() noexcept;
        [[nodiscard]] expected_void_t stop() noexcept;

        [[nodiscard]] expected_void_t shutdown() noexcept { return stop(); }
        [[nodiscard]] server_event_task_t serve_once() noexcept(false)
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
#endif // KMX_AIO_FEATURE_KNX
