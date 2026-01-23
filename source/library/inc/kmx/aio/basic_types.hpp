#pragma once
#ifndef PCH
    #include <cerrno>
    #include <cstddef>
    #include <cstdint>
    #include <sys/epoll.h>
    #include <system_error>
#endif

namespace kmx::aio
{
    using fd_t = int;

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
