/// @file aio/readiness/udp/socket.hpp
/// @brief Readiness-model UDP socket using epoll-based async I/O.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <expected>
    #include <sys/socket.h>
    #include <system_error>

    #include <kmx/aio/basic_types.hpp>
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
        using result_task = task<std::expected<std::size_t, std::error_code>>;
        using create_result = std::expected<socket, std::error_code>;

        [[nodiscard]] static create_result create(executor& exec, int domain = AF_INET,
                                                  int type = SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, int protocol = 0) noexcept;

        socket(executor& exec, file_descriptor&& fd) noexcept: io_base(exec, std::move(fd)) {}
        ~socket() override = default;
        socket(socket&&) noexcept = default;
        socket& operator=(socket&&) noexcept = delete;

        [[nodiscard]] result_task recvmsg(::msghdr* msg, int flags = 0) noexcept(false);
        [[nodiscard]] result_task sendmsg(const ::msghdr* msg, int flags = 0) noexcept(false);
    };
} // namespace kmx::aio::readiness::udp

#ifndef PCH
    #include <kmx/aio/readiness/udp/endpoint.hpp>
#endif
