/// @file aio/opc_ua/server.hpp
/// @brief Backend-neutral async OPC UA server facade.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <chrono>
    #include <expected>
    #include <memory>
    #include <system_error>

    #include <kmx/aio/opc_ua/types.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::opc_ua
{
    /// @brief Backend-neutral asynchronous OPC UA server facade.
    /// @details
    /// The server exposes coroutine-friendly lifecycle and iterate operations while
    /// hiding backend-specific setup/details.
    class server
    {
    public:
        /// @brief Construct a server with the provided runtime configuration.
        /// @param config Server listen/security/session configuration.
        explicit server(server_config config) noexcept;
        /// @brief Destroy the server and release backend resources.
        ~server() noexcept;

        /// @brief Non-copyable: the server owns its backend runtime.
        server(const server&) = delete;
        /// @brief Non-copyable: the server owns its backend runtime.
        server& operator=(const server&) = delete;
        /// @brief Move constructor — transfers ownership of the backend runtime.
        server(server&&) noexcept;
        /// @brief Move assignment — transfers ownership of the backend runtime.
        server& operator=(server&&) noexcept;

        /// @brief Start server runtime/listening endpoints asynchronously.
        /// @return Task resolving to success or error.
        [[nodiscard]] task_returning_expected_void_t start() noexcept(false);
        /// @brief Stop server runtime asynchronously.
        /// @return Task resolving to success or error.
        [[nodiscard]] task_returning_expected_void_t stop() noexcept(false);
        /// @brief Drive backend server iteration once.
        /// @param timeout Backend-dependent iterate/poll timeout.
        /// @return Task resolving to backend iterate work/result count.
        [[nodiscard]] task<std::expected<std::uint16_t, std::error_code>> iterate(std::chrono::milliseconds timeout) noexcept(false);

        /// @brief Access immutable server configuration.
        /// @return Reference to active configuration.
        [[nodiscard]] const server_config& config() const noexcept;
        /// @brief Access runtime statistics counters.
        /// @return Reference to cumulative statistics snapshot.
        [[nodiscard]] const statistics& get_stats() const noexcept;

    private:
        struct impl;
        /// @brief The backend implementation, kept opaque so the public header stays free of backend types.
        std::unique_ptr<impl> impl_;
    };

} // namespace kmx::aio::opc_ua
