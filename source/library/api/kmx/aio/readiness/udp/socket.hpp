/// @file aio/readiness/udp/socket.hpp
/// @brief Readiness-model UDP socket using epoll-based async I/O.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <expected>
    #include <sys/socket.h>
    #include <system_error>

    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/readiness/io_base.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::readiness::udp
{
    /// @brief Asynchronous UDP Socket.
    class socket: public io_base
    {
    public:
        /// @brief A constructed @ref socket, or the error code explaining why one could not be created.
        using create_result = std::expected<socket, std::error_code>;

        /// @brief Creates and registers a UDP socket.
        /// @param exec     The executor the descriptor is registered with.
        /// @param domain   Address family (`AF_INET` or `AF_INET6`).
        /// @param type     Socket type and flags passed to `socket()`.
        /// @param protocol Protocol selector passed to `socket()`.
        /// @return The created socket, or an error code.
        [[nodiscard]] static create_result create(executor& exec, const int domain = AF_INET,
                                                  int type = SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, int protocol = 0) noexcept;

        /// @brief Adopts an existing UDP socket descriptor.
        /// @param exec The executor the descriptor is registered with.
        /// @param fd   The socket descriptor.
        socket(executor& exec, file_descriptor&& fd) noexcept: io_base(exec, std::move(fd)) {}
        /// @brief Unregisters the descriptor and closes the socket.
        ~socket() override = default;
        /// @brief Move constructor — transfers ownership of the descriptor.
        socket(socket&&) noexcept = default;
        /// @brief Move assignment is disabled: the executor reference cannot be reseated.
        socket& operator=(socket&&) noexcept = delete;

        /// @brief Suspends until the socket is readable, then receives one message.
        /// @param msg   Message descriptor for buffers and ancillary data.
        /// @param flags Flags forwarded to `recvmsg`.
        /// @return A task yielding the number of bytes received, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] task_returning_expected_size_t recvmsg(::msghdr* msg, int flags = 0) noexcept(false);
        /// @brief Suspends until the socket is writable, then sends one message.
        /// @param msg   Message descriptor for buffers and ancillary data.
        /// @param flags Flags forwarded to `sendmsg`.
        /// @return A task yielding the number of bytes sent, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] task_returning_expected_size_t sendmsg(const ::msghdr* msg, int flags = 0) noexcept(false);
    };
} // namespace kmx::aio::readiness::udp

#ifndef PCH
#endif
