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
    #include <sys/epoll.h>
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

    /// @brief Standard integer types within aio namespace.
    using event_mask_t = std::uint32_t;

    /// @brief Represents the type of I/O event to wait for in coroutine suspension.
    /// @note Only read and write are used; errors are handled implicitly via epoll masks.
    enum class event_type : std::uint8_t
    {
        read,
        write
    };

    /// @brief Type-safe epoll event mask enumeration.
    enum class epoll_event_mask : event_mask_t
    {
        read = EPOLLIN,
        write = EPOLLOUT,
        error = EPOLLERR,
        hang_up = EPOLLHUP,
        edge_triggered = EPOLLET,
        one_shot = EPOLLONESHOT,
        remote_hang_up = EPOLLRDHUP
    };

    /// @brief Default epoll event mask: read, write, error, hang_up, edge-triggered.
    inline constexpr event_mask_t default_epoll_events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLET;

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

    [[nodiscard]] inline ip_address_owned_t to_owned_ip_address(const ip_address_t ip) noexcept
    {
        return std::visit(
            [](const auto& bytes) noexcept -> ip_address_owned_t
            {
                using ip_t = std::decay_t<decltype(bytes)>;
                if constexpr (std::is_same_v<ip_t, ipv4_address_t>)
                {
                    ipv4_address_owned_t ip4 {};
                    std::memcpy(ip4.data(), bytes.data(), ip4.size());
                    return ip4;
                }

                ipv6_address_owned_t ip6 {};
                std::memcpy(ip6.data(), bytes.data(), ip6.size());
                return ip6;
            },
            ip);
    }

    [[nodiscard]] inline ip_address_t to_ip_address_view(const ip_address_owned_t& ip) noexcept
    {
        return std::visit(
            [](const auto& bytes) noexcept -> ip_address_t
            {
                using ip_t = std::decay_t<decltype(bytes)>;
                if constexpr (std::is_same_v<ip_t, ipv4_address_owned_t>)
                    return ipv4_address_t {bytes};
                else
                    return ipv6_address_t {bytes};
            },
            ip);
    }

    [[nodiscard]] inline std::string ip_to_string(const ip_address_t ip) noexcept
    {
        char buffer[INET6_ADDRSTRLEN] {};

        const bool ok = std::visit(
            [&buffer](const auto& bytes) noexcept
            {
                using ip_t = std::decay_t<decltype(bytes)>;
                if constexpr (std::is_same_v<ip_t, ipv4_address_t>)
                {
                    in_addr addr {};
                    std::memcpy(&addr, bytes.data(), bytes.size());
                    return ::inet_ntop(AF_INET, &addr, buffer, sizeof(buffer)) != nullptr;
                }

                in6_addr addr {};
                std::memcpy(&addr, bytes.data(), bytes.size());
                return ::inet_ntop(AF_INET6, &addr, buffer, sizeof(buffer)) != nullptr;
            },
            ip);

        return ok ? std::string(buffer) : std::string {};
    }

    [[nodiscard]] inline std::expected<socket_address, std::error_code> make_socket_address(const ip_address_t ip,
                                                                                            const port_t port) noexcept
    {
        socket_address result {};

        std::visit(
            [&result, port](const auto& bytes) noexcept
            {
                using ip_t = std::decay_t<decltype(bytes)>;
                if constexpr (std::is_same_v<ip_t, ipv4_address_t>)
                {
                    auto* addr = reinterpret_cast<::sockaddr_in*>(&result.storage);
                    addr->sin_family = AF_INET;
                    addr->sin_port = ::htons(port);
                    std::memcpy(&addr->sin_addr, bytes.data(), bytes.size());
                    result.length = sizeof(::sockaddr_in);
                }
                else
                {
                    auto* addr = reinterpret_cast<sockaddr_in6*>(&result.storage);
                    addr->sin6_family = AF_INET6;
                    addr->sin6_port = ::htons(port);
                    std::memcpy(&addr->sin6_addr, bytes.data(), bytes.size());
                    result.length = sizeof(sockaddr_in6);
                }
            },
            ip);

        return result;
    }

    [[nodiscard]] inline std::expected<socket_address, std::error_code> make_socket_address(const ip_address_owned_t& ip,
                                                                                            const port_t port) noexcept
    {
        return make_socket_address(to_ip_address_view(ip), port);
    }

    [[nodiscard]] inline std::expected<endpoint_address, std::error_code> parse_socket_address(const socket_address& address) noexcept
    {
        if (address.length < sizeof(sockaddr))
            return std::unexpected(error_from_errno(EINVAL));

        const auto* addr = reinterpret_cast<const sockaddr*>(&address.storage);
        endpoint_address result {};
        switch(addr->sa_family)
        {
            case AF_INET:
            {
                if (address.length < sizeof(::sockaddr_in))
                    return std::unexpected(error_from_errno(EINVAL));

                const auto* addr4 = reinterpret_cast<const ::sockaddr_in*>(&address.storage);
                auto& ip4 = result.ip.emplace<ipv4_address_owned_t>();
                std::memcpy(ip4.data(), &addr4->sin_addr, ip4.size());
                result.port = ::ntohs(addr4->sin_port);
                return result;
            }
            case AF_INET6:
            {
                if (address.length < sizeof(sockaddr_in6))
                    return std::unexpected(error_from_errno(EINVAL));

                const auto* addr6 = reinterpret_cast<const sockaddr_in6*>(&address.storage);
                auto& ip6 = result.ip.emplace<ipv6_address_owned_t>();
                std::memcpy(ip6.data(), &addr6->sin6_addr, ip6.size());
                result.port = ::ntohs(addr6->sin6_port);
                return result;
            }
            default:
                return std::unexpected(error_from_errno(EAFNOSUPPORT));
        }
    }

    /// @brief Bitwise OR operator for epoll event masks (event_mask_t | epoll_event_mask).
    [[nodiscard]] constexpr event_mask_t operator|(const event_mask_t a, const epoll_event_mask b) noexcept
    {
        return a | static_cast<event_mask_t>(b);
    }

    /// @brief Bitwise OR operator for epoll event masks (epoll_event_mask | event_mask_t).
    [[nodiscard]] constexpr event_mask_t operator|(const epoll_event_mask a, const event_mask_t b) noexcept
    {
        return static_cast<event_mask_t>(a) | b;
    }

    /// @brief Bitwise OR operator for epoll event masks (epoll_event_mask | epoll_event_mask).
    [[nodiscard]] constexpr event_mask_t operator|(const epoll_event_mask a, const epoll_event_mask b) noexcept
    {
        return static_cast<event_mask_t>(a) | static_cast<event_mask_t>(b);
    }

    /// @brief Bitwise AND operator for epoll event masks (event_mask_t & epoll_event_mask).
    [[nodiscard]] constexpr event_mask_t operator&(const event_mask_t a, const epoll_event_mask b) noexcept
    {
        return a & static_cast<event_mask_t>(b);
    }

    /// @brief Bitwise AND operator for epoll event masks (epoll_event_mask & event_mask_t).
    [[nodiscard]] constexpr event_mask_t operator&(const epoll_event_mask a, const event_mask_t b) noexcept
    {
        return static_cast<event_mask_t>(a) & b;
    }

    /// @brief Bitwise AND operator for epoll event masks (epoll_event_mask & epoll_event_mask).
    [[nodiscard]] constexpr event_mask_t operator&(const epoll_event_mask a, const epoll_event_mask b) noexcept
    {
        return static_cast<event_mask_t>(a) & static_cast<event_mask_t>(b);
    }

    /// @brief Bitwise NOT operator for epoll event masks.
    [[nodiscard]] constexpr event_mask_t operator~(const epoll_event_mask a) noexcept
    {
        return ~static_cast<event_mask_t>(a);
    }

    /// @brief Bitwise XOR operator for epoll event masks (event_mask_t ^ epoll_event_mask).
    [[nodiscard]] constexpr event_mask_t operator^(const event_mask_t a, const epoll_event_mask b) noexcept
    {
        return a ^ static_cast<event_mask_t>(b);
    }

    /// @brief Bitwise XOR operator for epoll event masks (epoll_event_mask ^ event_mask_t).
    [[nodiscard]] constexpr event_mask_t operator^(const epoll_event_mask a, const event_mask_t b) noexcept
    {
        return static_cast<event_mask_t>(a) ^ b;
    }

    /// @brief Bitwise XOR operator for epoll event masks (epoll_event_mask ^ epoll_event_mask).
    [[nodiscard]] constexpr event_mask_t operator^(const epoll_event_mask a, const epoll_event_mask b) noexcept
    {
        return static_cast<event_mask_t>(a) ^ static_cast<event_mask_t>(b);
    }
} // namespace kmx::aio
