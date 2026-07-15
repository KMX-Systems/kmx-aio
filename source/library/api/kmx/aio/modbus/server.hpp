/// @file aio/modbus/server.hpp
/// @brief Asynchronous Modbus TCP server facade.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#if defined(KMX_AIO_FEATURE_MODBUS)
    #ifndef PCH
        #include <cstdint>
        #include <expected>
        #include <functional>
        #include <memory>
        #include <system_error>
        #include <vector>
    #endif

    #include <kmx/aio/modbus/types.hpp>
    #include <kmx/aio/task.hpp>

namespace kmx::aio::readiness
{
    class executor;
}

namespace kmx::aio::modbus
{
    /// @brief Per-request data delivered to a @ref request_handler.
    struct server_request
    {
        /// @brief Unit identifier from the request MBAP header.
        std::uint8_t unit_id = 0u;
        /// @brief Raw PDU bytes beginning with the function code byte.
        std::vector<std::uint8_t> pdu;
    };

    /// @brief Callback type for handling one Modbus request.
    /// @details The handler receives a @ref server_request and must return
    /// the raw response PDU (function code byte + data).  To return a
    /// Modbus exception, set the high bit of the function code and append a
    /// single exception code byte.
    using request_handler =
        std::function<task<std::vector<std::uint8_t>>(server_request)>;

    /// @brief Asynchronous Modbus TCP server.
    /// @details
    /// Listens for incoming TCP connections, reads Modbus ADUs from each
    /// client, dispatches them to registered per-function-code handlers,
    /// and writes response ADUs.  Multiple simultaneous clients are
    /// supported; each connection is handled in an independent coroutine
    /// spawned on the provided executor.
    class server
    {
    public:
        server() noexcept;
        ~server() noexcept;

        server(const server&) = delete;
        server& operator=(const server&) = delete;
        server(server&&) noexcept;
        server& operator=(server&&) noexcept;

        /// @brief Register a handler for a specific function code.
        /// @details Replaces any previously registered handler for @p fc.
        ///          Unregistered function codes receive an automatic
        ///          @c illegal_function exception response.
        /// @param fc      Function code to handle.
        /// @param handler Coroutine callback invoked per matching request.
        void set_handler(function_code fc, request_handler handler);

        /// @brief Start accepting connections and serving requests.
        /// @details Binds and listens on the address/port in @p config, then
        ///          loops accepting clients until @ref stop is called or an
        ///          error occurs.
        /// @param exec   Executor that drives the accept and connection coroutines.
        /// @param config Server bind address, port, and unit identifier.
        /// @return Task that completes when the server has stopped.
        [[nodiscard]] task<std::expected<void, std::error_code>>
        serve(readiness::executor& exec, server_config config) noexcept(false);

        /// @brief Signal the server to stop accepting new connections.
        void stop() noexcept;

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };

} // namespace kmx::aio::modbus
#endif // KMX_AIO_FEATURE_MODBUS
