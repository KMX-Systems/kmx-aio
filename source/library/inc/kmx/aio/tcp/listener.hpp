/// @file aio/tcp/listener.hpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <expected>

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/executor.hpp>
    #include <kmx/aio/io_base.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::tcp
{
    /// @brief Asynchronous TCP listener.
    class listener: public io_base
    {
    public:
        /// @brief Result type used by non-coroutine operations.
        using result_t = std::expected<void, std::error_code>;

        /// @brief Creates a listener bound to the specified IP and port.
        /// @throws std::system_error If socket creation or bind fails.
        listener(executor& exec, const ip_address_t ip, const port_t port) noexcept(false);
        /// @brief Destroys the listener and unregisters descriptor if needed.
        ~listener() override = default;
        /// @brief Move constructor.
        listener(listener&&) noexcept = default;
        /// @brief Move assignment is disabled because base stores executor reference.
        listener& operator=(listener&&) noexcept = delete;

        /// @brief Starts listening.
        /// @return Result or error code. Does not throw.
        result_t listen(const int backlog = 128) noexcept;

        /// @brief Asynchronously accepts a new connection.
        /// @return A task yielding the client file descriptor.
        /// @throws std::bad_alloc (Corountine frame allocation).
        task<std::expected<descriptor::file, std::error_code>> accept() noexcept(false);
    };

} // namespace kmx::aio::tcp
