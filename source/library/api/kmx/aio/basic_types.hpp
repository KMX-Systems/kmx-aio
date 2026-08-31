/// @file aio/basic_types.hpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <arpa/inet.h>
    #include <cerrno>
    #include <cstring>
    #include <expected>
    #include <netinet/in.h>
    #include <span>
    #include <string>
    #include <sys/socket.h>
    #include <system_error>
    #include <variant>

    #include <kmx/aio/ipv4.hpp>
    #include <kmx/aio/ipv6.hpp>
#endif

namespace kmx::aio
{
    /// @brief Non-owning mutable span of characters.
    using span_char_t = std::span<char>;
    /// @brief Non-owning read-only span of characters.
    using cspan_char_t = std::span<const char>;

    /// @brief Non-owning mutable span of bytes.
    using span_uint8_t = std::span<std::uint8_t>;
    /// @brief Non-owning read-only span of bytes.
    using cspan_uint8_t = std::span<const std::uint8_t>;

    /// @brief Non-owning mutable span of bytes.
    using span_byte_t = std::span<std::byte>;
    /// @brief Non-owning read-only span of bytes.
    using cspan_byte_t = std::span<const std::byte>;

    /// @brief Owned IP address variant covering IPv4 and IPv6.
    using ip_address_owned_t = std::variant<ipv4::address_owned_t, ipv6::address_owned_t>;
    /// @brief Non-owning IP address view variant.
    using ip_address_t = std::variant<ipv4::address_t, ipv6::address_t>;

    /// @brief Result of an operation yielding a boolean, or an error code.
    using expected_bool_t = std::expected<bool, std::error_code>;
    /// @brief Result of an operation yielding an integer, or an error code.
    using expected_int_t = std::expected<int, std::error_code>;
    /// @brief Result of an operation yielding a byte count, or an error code.
    using expected_size_t = std::expected<std::size_t, std::error_code>;
    /// @brief Result of an operation yielding nothing, or an error code.
    using expected_void_t = std::expected<void, std::error_code>;

    /// @brief Creates a non-owning IP address view from IPv4 storage.
    /// @param ip The owned IPv4 bytes.
    /// @return An IPv4 address variant view.
    [[nodiscard]] constexpr ip_address_t make_ip_address(const ipv4::storage_t& ip) noexcept
    {
        return ipv4::make_address(ip);
    }

    /// @brief Creates a non-owning IP address view from IPv6 storage.
    /// @param ip The owned IPv6 bytes.
    /// @return An IPv6 address variant view.
    [[nodiscard]] constexpr ip_address_t make_ip_address(const ipv6::storage_t& ip) noexcept
    {
        return ipv6::make_address(ip);
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

    /// @brief An @ref endpoint_address, or the error code explaining why one could not be produced.
    using expected_endpoint_address_t = std::expected<endpoint_address, std::error_code>;

    /// @brief Binary socket address storage plus length.
    struct socket_address
    {
        /// @brief Backing sockaddr storage.
        ::sockaddr_storage storage {};
        /// @brief Valid length of the stored address.
        ::socklen_t length {};
    };

    /// @brief A @ref socket_address, or the error code explaining why one could not be produced.
    using expected_socket_address_t = std::expected<socket_address, std::error_code>;

    /// @brief Helper to check if an error code represents a non-blocking operation that would block.
    /// @param ec The error code to inspect.
    /// @return `true` if the error represents a would-block condition.
    [[nodiscard]] constexpr bool would_block(const std::error_code& ec) noexcept
    {
        const auto value = ec.value();
        // LCOV_EXCL_BR_LINE: EAGAIN and EWOULDBLOCK are the same number on Linux, so the second
        // comparison can never be the one that decides. Both are named because they are not the same
        // number everywhere.
        return (value == EAGAIN) || (value == EWOULDBLOCK); // LCOV_EXCL_BR_LINE
    }

    /// @brief Helper to check if an error code represents a non-blocking operation that would block.
    /// @param err The errno value to inspect.
    /// @return `true` if the errno represents a would-block condition.
    [[nodiscard]] constexpr bool would_block(const int err) noexcept
    {
        // LCOV_EXCL_BR_LINE: as above - one value, two names, on this platform.
        return (err == EAGAIN) || (err == EWOULDBLOCK); // LCOV_EXCL_BR_LINE
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
    [[nodiscard]] constexpr int ip_family(const ip_address_t ip) noexcept
    {
        return std::holds_alternative<ipv4::address_t>(ip) ? AF_INET : AF_INET6;
    }

    /// @brief Returns the address family for owned IP storage.
    /// @param ip The owned IP address.
    /// @return `AF_INET` for IPv4 or `AF_INET6` for IPv6.
    [[nodiscard]] constexpr int ip_family(const ip_address_owned_t& ip) noexcept
    {
        return std::holds_alternative<ipv4::address_owned_t>(ip) ? AF_INET : AF_INET6;
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
    [[nodiscard]] expected_socket_address_t make_socket_address(const ip_address_t ip, const port_t port) noexcept;

    /// @brief Builds a socket address from owned IP storage and port.
    /// @param ip The owned IP address.
    /// @param port The port number.
    /// @return A socket address or an error.
    [[nodiscard]] expected_socket_address_t make_socket_address(const ip_address_owned_t& ip, const port_t port) noexcept;

    /// @brief Parses a socket address into owned endpoint storage.
    /// @param address The socket address to parse.
    /// @return An owned endpoint representation or an error.
    [[nodiscard]] expected_endpoint_address_t parse_socket_address(const socket_address& address) noexcept;

} // namespace kmx::aio
