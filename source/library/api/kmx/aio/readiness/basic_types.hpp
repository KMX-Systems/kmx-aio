/// @file aio/readiness/basic_types.hpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <sys/epoll.h>

    #include <kmx/aio/basic_types.hpp>
#endif

namespace kmx::aio::readiness
{
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
}