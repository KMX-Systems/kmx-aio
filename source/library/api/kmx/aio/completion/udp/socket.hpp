/// @file aio/completion/udp/socket.hpp
/// @brief Completion-model UDP socket using io_uring for async recvmsg/sendmsg.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <expected>
    #include <sys/socket.h>
    #include <system_error>

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/completion/io_base.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::completion::udp
{
    /// @brief Asynchronous UDP socket backed by io_uring completion I/O.
    /// @details Submits recvmsg/sendmsg operations to io_uring, allowing the kernel
    ///          to complete the datagram transfer before waking the coroutine.
    class socket: public io_base
    {
    public:
        /// @brief Task type for recv/send operations.
        using result_task = task<std::expected<std::size_t, std::error_code>>;

        /// @brief Result type for factory creation.
        using create_result = std::expected<socket, std::error_code>;

        /// @brief Creates a non-blocking UDP socket.
        /// @param exec     The completion executor.
        /// @param domain   Socket domain (AF_INET or AF_INET6).
        /// @param type     Socket type flags.
        /// @param protocol Protocol number.
        /// @return A socket on success, or an error code.
        [[nodiscard]] static create_result create(std::shared_ptr<executor> exec, int domain = AF_INET,
                                                  int type = SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, int protocol = 0) noexcept;

        /// @brief Constructs a socket from an executor and file descriptor.
        /// @param exec The completion executor.
        /// @param fd   UDP socket descriptor (ownership transferred).
        socket(std::shared_ptr<executor> exec, file_descriptor&& fd) noexcept: io_base(std::move(exec), std::move(fd)) {}

        /// @brief Destructor.
        ~socket() noexcept = default;

        /// @brief Non-copyable.
        socket(const socket&) = delete;
        /// @brief Non-copyable.
        socket& operator=(const socket&) = delete;

        /// @brief Move constructor.
        socket(socket&&) noexcept = default;
        /// @brief Move assignment is disabled.
        socket& operator=(socket&&) noexcept = delete;

        /// @brief Asynchronously receives a datagram via io_uring.
        /// @param msg   Message header describing payload buffers and peer address.
        /// @param flags Flags forwarded to recvmsg.
        /// @return Number of bytes received, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] result_task recvmsg(::msghdr* msg, unsigned flags = 0u) noexcept(false);

        /// @brief Asynchronously sends a datagram via io_uring.
        /// @param msg   Message header describing payload buffers and peer address.
        /// @param flags Flags forwarded to sendmsg.
        /// @return Number of bytes sent, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] result_task sendmsg(const ::msghdr* msg, unsigned flags = 0u) noexcept(false);

        /// @brief Binds the socket to an address and port.
        /// @param ip   The IP address to bind to.
        /// @param port The port to bind to.
        /// @return Success or an error code.
        [[nodiscard]] std::expected<void, std::error_code> bind(ip_address_t ip, port_t port) noexcept;
    };

} // namespace kmx::aio::completion::udp
