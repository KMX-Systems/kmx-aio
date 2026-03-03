#pragma once
#ifndef PCH
    #include <expected>
    #include <sys/socket.h>
    #include <system_error>

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/io_base.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::udp
{
    /// @brief Asynchronous UDP Socket.
    /// @details Low-level datagram API based on recvmsg/sendmsg.
    class socket: public io_base
    {
    public:
        /// @brief Task type used by UDP read/write operations.
        using result_task = task<std::expected<std::size_t, std::error_code>>;
        /// @brief Alias for the result of create().
        using create_result = std::expected<socket, std::error_code>;

        /// @brief Creates a non-blocking UDP socket and registers it with the executor.
        /// @param exec     Executor used for registration and wait_io.
        /// @param domain   Socket domain, e.g. AF_INET or AF_INET6.
        /// @param type     Socket type flags, defaults to SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC.
        /// @param protocol Protocol number, usually 0.
        /// @return Socket on success, or error code.
        [[nodiscard]] static create_result create(executor& exec, const int domain = AF_INET,
                                                  const int type = SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                                                  const int protocol = 0) noexcept;

        /// @brief Constructs a socket wrapper from executor and owned descriptor.
        /// @param exec Executor used for wait_io suspension.
        /// @param fd   UDP socket descriptor owner.
        socket(executor& exec, descriptor::file&& fd) noexcept: io_base(exec, std::move(fd)) {}
        /// @brief Destroys the socket wrapper.
        ~socket() override = default;

        /// @brief Move constructor.
        socket(socket&&) noexcept = default;
        /// @brief Move assignment is disabled because base stores executor reference.
        socket& operator=(socket&&) noexcept = delete;

        /// @brief Asynchronously receives a message.
        /// @param msg   Message header describing payload buffers, peer address, and ancillary data.
        /// @param flags Flags forwarded to ::recvmsg.
        /// @return Number of received bytes, or error code.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] result_task recvmsg(struct msghdr* msg, const int flags = 0) noexcept(false);

        /// @brief Asynchronously sends a message.
        /// @param msg   Message header describing payload buffers, peer address, and ancillary data.
        /// @param flags Flags forwarded to ::sendmsg.
        /// @return Number of sent bytes, or error code.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] result_task sendmsg(const struct msghdr* msg, const int flags = 0) noexcept(false);
    };
} // namespace kmx::aio::udp
