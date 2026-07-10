/// @file aio/basic_types.hpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <arpa/inet.h>
    #include <cerrno>
    #include <cstring>
    #include <expected>
    #include <netinet/in.h>
    #include <string>
    #include <sys/socket.h>
    #include <system_error>
    #include <type_traits>
    #include <variant>

    #include <kmx/aio/ipv4.hpp>
    #include <kmx/aio/ipv6.hpp>
#endif

namespace kmx::aio
{
    /// @brief Owned IP address variant covering IPv4 and IPv6.
    using ip_address_owned_t = std::variant<ipv4_address_owned_t, ipv6_address_owned_t>;
    /// @brief Non-owning IP address view variant.
    using ip_address_t = std::variant<ipv4_address_t, ipv6_address_t>;

    /// @brief Creates a non-owning IP address view from IPv4 storage.
    /// @param ip The owned IPv4 bytes.
    /// @return An IPv4 address variant view.
    [[nodiscard]] constexpr ip_address_t make_ip_address(const ipv4_storage_t& ip) noexcept
    {
        return make_ipv4_address(ip);
    }

    /// @brief Creates a non-owning IP address view from IPv6 storage.
    /// @param ip The owned IPv6 bytes.
    /// @return An IPv6 address variant view.
    [[nodiscard]] constexpr ip_address_t make_ip_address(const ipv6_storage_t& ip) noexcept
    {
        return make_ipv6_address(ip);
    }

    /// @brief File descriptor alias used throughout the library.
    using fd_t = int;
    /// @brief Port alias used throughout the library.
    using port_t = std::uint16_t;

    /// @brief Owned socket endpoint consisting of IP storage and port.
    struct endpoint_address
    {
        /// @brief The endpoint IP address.
        ip_address_owned_t ip {};
        /// @brief The endpoint port.
        port_t port {};
    };

    /// @brief Binary socket address storage plus length.
    struct socket_address
    {
        /// @brief Backing sockaddr storage.
        ::sockaddr_storage storage {};
        /// @brief Valid length of the stored address.
        ::socklen_t length {};
    };

    /// @brief Helper to check if an error code represents a non-blocking operation that would block.
    /// @param ec The error code to inspect.
    /// @return `true` if the error represents a would-block condition.
    [[nodiscard]] inline constexpr bool would_block(const std::error_code& ec) noexcept
    {
        const auto value = ec.value();
        return (value == EAGAIN) || (value == EWOULDBLOCK);
    }

    /// @brief Helper to check if an error code represents a non-blocking operation that would block.
    /// @param err The errno value to inspect.
    /// @return `true` if the errno represents a would-block condition.
    [[nodiscard]] inline constexpr bool would_block(const int err) noexcept
    {
        return (err == EAGAIN) || (err == EWOULDBLOCK);
    }

    /// @brief Helper to create a std::error_code from the current errno.
    /// @return The current errno wrapped as a std::error_code.
    [[nodiscard]] inline std::error_code error_from_errno() noexcept
    {
        return std::error_code(errno, std::generic_category());
    }

    /// @brief Helper to create a std::error_code from a specific error number.
    /// @param err The errno value.
    /// @return The errno wrapped as a std::error_code.
    [[nodiscard]] inline std::error_code error_from_errno(const int err) noexcept
    {
        return std::error_code(err, std::generic_category());
    }

    /// @brief Returns the address family for an IP view.
    /// @param ip The IP address view.
    /// @return `AF_INET` for IPv4 or `AF_INET6` for IPv6.
    [[nodiscard]] inline int ip_family(const ip_address_t ip) noexcept
    {
        return std::holds_alternative<ipv4_address_t>(ip) ? AF_INET : AF_INET6;
    }

    /// @brief Returns the address family for owned IP storage.
    /// @param ip The owned IP address.
    /// @return `AF_INET` for IPv4 or `AF_INET6` for IPv6.
    [[nodiscard]] inline int ip_family(const ip_address_owned_t& ip) noexcept
    {
        return std::holds_alternative<ipv4_address_owned_t>(ip) ? AF_INET : AF_INET6;
    }

    /// @brief Copies a view IP address into owned storage.
    /// @param ip The non-owning IP address view.
    /// @return Owned IP storage with copied bytes.
    [[nodiscard]] ip_address_owned_t to_owned_ip_address(const ip_address_t ip) noexcept;

    /// @brief Creates a non-owning view of owned IP storage.
    /// @param ip The owned IP storage.
    /// @return A non-owning IP view.
    [[nodiscard]] ip_address_t to_ip_address_view(const ip_address_owned_t& ip) noexcept;

    /// @brief Converts an IP address into human-readable text.
    /// @param ip The IP address view.
    /// @return The textual IP representation.
    [[nodiscard]] std::string ip_to_string(const ip_address_t ip) noexcept;

    /// @brief Builds a socket address from an IP view and port.
    /// @param ip The IP address view.
    /// @param port The port number.
    /// @return A socket address or an error.
    [[nodiscard]] std::expected<socket_address, std::error_code> make_socket_address(const ip_address_t ip, const port_t port) noexcept;

    /// @brief Builds a socket address from owned IP storage and port.
    /// @param ip The owned IP address.
    /// @param port The port number.
    /// @return A socket address or an error.
    [[nodiscard]] std::expected<socket_address, std::error_code> make_socket_address(const ip_address_owned_t& ip,
                                                                                     const port_t port) noexcept;

    /// @brief Parses a socket address into owned endpoint storage.
    /// @param address The socket address to parse.
    /// @return An owned endpoint representation or an error.
    [[nodiscard]] std::expected<endpoint_address, std::error_code> parse_socket_address(const socket_address& address) noexcept;

} // namespace kmx::aio
