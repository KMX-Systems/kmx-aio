/// @file inc/kmx/aio/test/fd_pair.hpp
/// @brief Ephemeral-port binding, shared by the tests.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>

    #include <cerrno>
    #include <expected>
    #include <system_error>
    #include <netinet/in.h>
    #include <sys/socket.h>
#endif

namespace kmx::aio::test
{
    /// @brief Binds @p fd to a loopback port the kernel picks, and reports which one.
    /// @details Hard-coding a port makes a test fail when the machine happens to be using it, and makes
    ///          two tests in the same binary collide. Binding port 0 and asking afterwards does not.
    /// @param fd An unbound AF_INET socket.
    /// @return The bound port in host order, or the errno that bind() or getsockname() reported.
    [[nodiscard]] inline std::expected<port_t, std::error_code> bind_ephemeral_port(const int fd) noexcept
    {
        ::sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        address.sin_port = 0u;

        if (::bind(fd, reinterpret_cast<const ::sockaddr*>(&address), sizeof(address)) != 0)
            return std::unexpected(std::error_code(errno, std::system_category()));

        ::sockaddr_in bound {};
        auto length = static_cast<::socklen_t>(sizeof(bound));
        if (::getsockname(fd, reinterpret_cast<::sockaddr*>(&bound), &length) != 0)
            return std::unexpected(std::error_code(errno, std::system_category()));

        return ::ntohs(bound.sin_port);
    }

}
