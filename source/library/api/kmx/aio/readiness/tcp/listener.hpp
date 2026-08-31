/// @file aio/readiness/tcp/listener.hpp
/// @brief Readiness-model TCP listener using epoll-based async accept.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/readiness/io_base.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::readiness::tcp
{
    /// @brief Asynchronous TCP listener.
    class listener: public io_base
    {
    public:
        /// @brief Creates a listening socket bound to the given address.
        /// @param exec The executor the socket is registered with.
        /// @param ip   The local IP address to bind to.
        /// @param port The local TCP port to bind to.
        /// @throws std::system_error If the socket could not be created, bound, or registered.
        listener(executor& exec, ip_address_t ip, port_t port) noexcept(false);
        /// @brief Unregisters the descriptor and closes the listening socket.
        ~listener() override = default;
        /// @brief Move constructor — transfers ownership of the descriptor.
        listener(listener&&) noexcept = default;
        /// @brief Move assignment is disabled: the executor reference cannot be reseated.
        listener& operator=(listener&&) noexcept = delete;

        /// @brief Marks the socket as accepting connections.
        /// @param backlog Maximum number of pending connections the kernel may queue.
        /// @return Success, or the error `listen` reported.
        expected_void_t listen(const int backlog = 128) noexcept;
        /// @brief Suspends until a connection arrives, then accepts it.
        /// @return A task yielding the accepted connection's descriptor, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        task<file_descriptor::expected_t> accept() noexcept(false);
    };

} // namespace kmx::aio::readiness::tcp
