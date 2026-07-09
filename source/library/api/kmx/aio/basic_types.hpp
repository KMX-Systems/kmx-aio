/// @file aio/basic_types.hpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <arpa/inet.h>
    #include <array>
    #include <cerrno>
    #include <cstdint>
    #include <cstring>
    #include <expected>
    #include <netinet/in.h>
    #include <span>
    #include <string>
    #include <sys/socket.h>
    #include <system_error>
    #include <type_traits>
    #include <variant>
#endif

namespace kmx::aio
{
    using ipv4_storage_t = std::array<std::uint8_t, 4u>;
    using ipv6_storage_t = std::array<std::uint8_t, 16u>;
    using ipv4_address_owned_t = ipv4_storage_t;
    using ipv6_address_owned_t = ipv6_storage_t;
    using ip_address_owned_t = std::variant<ipv4_address_owned_t, ipv6_address_owned_t>;
    using ipv4_address_t = std::span<const std::uint8_t, 4u>;
    using ipv6_address_t = std::span<const std::uint8_t, 16u>;
    using ip_address_t = std::variant<ipv4_address_t, ipv6_address_t>;

    inline constexpr ipv4_storage_t localhost_ipv4 {127u, 0u, 0u, 1u};
    inline constexpr ipv4_storage_t any_ipv4 {0u, 0u, 0u, 0u};

    [[nodiscard]] constexpr ipv4_address_t make_ipv4_address(const ipv4_storage_t& ip) noexcept
    {
        return ipv4_address_t {ip};
    }

    [[nodiscard]] constexpr ipv6_address_t make_ipv6_address(const ipv6_storage_t& ip) noexcept
    {
        return ipv6_address_t {ip};
    }

    [[nodiscard]] constexpr ip_address_t make_ip_address(const ipv4_storage_t& ip) noexcept
    {
        return make_ipv4_address(ip);
    }

    [[nodiscard]] constexpr ip_address_t make_ip_address(const ipv6_storage_t& ip) noexcept
    {
        return make_ipv6_address(ip);
    }

    using fd_t = int;
    using port_t = std::uint16_t;

    struct endpoint_address
    {
        ip_address_owned_t ip {};
        port_t port {};
    };

    struct socket_address
    {
        ::sockaddr_storage storage {};
        ::socklen_t length {};
    };

    /// @brief Helper to check if an error code represents a non-blocking operation that would block.
    [[nodiscard]] inline constexpr bool would_block(const std::error_code& ec) noexcept
    {
        const auto value = ec.value();
        return (value == EAGAIN) || (value == EWOULDBLOCK);
    }

    /// @brief Helper to check if an error code represents a non-blocking operation that would block.
    [[nodiscard]] inline constexpr bool would_block(const int err) noexcept
    {
        return (err == EAGAIN) || (err == EWOULDBLOCK);
    }

    /// @brief Helper to create a std::error_code from the current errno.
    [[nodiscard]] inline std::error_code error_from_errno() noexcept
    {
        return std::error_code(errno, std::generic_category());
    }

    /// @brief Helper to create a std::error_code from a specific error number.
    [[nodiscard]] inline std::error_code error_from_errno(const int err) noexcept
    {
        return std::error_code(err, std::generic_category());
    }

    [[nodiscard]] inline int ip_family(const ip_address_t ip) noexcept
    {
        return std::holds_alternative<ipv4_address_t>(ip) ? AF_INET : AF_INET6;
    }

    [[nodiscard]] inline int ip_family(const ip_address_owned_t& ip) noexcept
    {
        return std::holds_alternative<ipv4_address_owned_t>(ip) ? AF_INET : AF_INET6;
    }

    [[nodiscard]] ip_address_owned_t to_owned_ip_address(const ip_address_t ip) noexcept;

    [[nodiscard]] ip_address_t to_ip_address_view(const ip_address_owned_t& ip) noexcept;

    [[nodiscard]] std::string ip_to_string(const ip_address_t ip) noexcept;

    [[nodiscard]] std::expected<socket_address, std::error_code> make_socket_address(const ip_address_t ip,
                                                                                      const port_t port) noexcept;

    [[nodiscard]] std::expected<socket_address, std::error_code> make_socket_address(const ip_address_owned_t& ip,
                                                                                      const port_t port) noexcept;

    [[nodiscard]] std::expected<endpoint_address, std::error_code> parse_socket_address(const socket_address& address) noexcept;

} // namespace kmx::aio
