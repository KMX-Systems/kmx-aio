/// @file aio/readiness/descriptor/epoll.hpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <span>
    #include <vector>

    #include <kmx/aio/file_descriptor.hpp>
    #include <kmx/aio/readiness/basic_types.hpp>
#endif

namespace kmx::aio::readiness::descriptor
{
    /// @brief RAII wrapper for epoll file descriptors with type-safe operations.
    class epoll: public file_descriptor
    {
    public:
        epoll() noexcept = default;

        explicit epoll(const fd_t fd) noexcept: file_descriptor(fd) {}

        // Non-copyable
        epoll(const epoll&) = delete;
        epoll& operator=(const epoll&) = delete;

        // Move-only
        epoll(epoll&&) noexcept = default;
        epoll& operator=(epoll&&) noexcept = default;

        /// @brief Creates a new epoll instance.
        /// @param flags Flags for epoll_create1 (e.g. EPOLL_CLOEXEC).
        /// @return New epoll instance or error code.
        [[nodiscard]] static std::expected<epoll, std::error_code> create(const int flags = 0) noexcept;

        /// @brief Add a file descriptor to be monitored by this epoll instance.
        /// @param fd The file descriptor to monitor.
        /// @param events The events to monitor (bitmask of epoll_event_mask).
        /// @return An error_code on failure, or void on success.
        [[nodiscard]] expected_void_t add_monitored_fd(const fd_t fd, const event_mask_t events = default_epoll_events) noexcept;

        /// @brief Modify the monitored events for a file descriptor.
        /// @param fd The file descriptor to modify.
        /// @param events The new events to monitor (bitmask of epoll_event_mask).
        /// @return An error_code on failure, or void on success.
        [[nodiscard]] expected_void_t modify_events(const fd_t fd, const event_mask_t events) noexcept;

        /// @brief Remove a file descriptor from epoll monitoring.
        /// @param fd The file descriptor to stop monitoring.
        /// @return An error_code on failure, or void on success.
        [[nodiscard]] expected_void_t remove_monitored_fd(const fd_t fd) noexcept;

        /// @brief Wait for events on monitored file descriptors, filling a caller-owned buffer.
        /// @param buffer Storage the kernel writes the ready events into.
        /// @param timeout_ms Timeout in milliseconds (-1 = indefinite).
        /// @return The number of events written to @p buffer, or an error_code on failure.
        /// @note For an event loop that waits over and over on the same buffer. The vector overload
        ///       below resizes its argument down to the number of events it received, so the next call
        ///       grows it back - and growing a vector value-initializes the elements it adds, which
        ///       means memsetting the whole buffer before every wait for values the kernel is about to
        ///       overwrite anyway. With max_events at its default that is twelve kilobytes of zeroing
        ///       per iteration of the loop.
        [[nodiscard]] expected_size_t wait_events(std::span<epoll_event> buffer, const int timeout_ms = -1) noexcept;

        /// @brief Wait for events on monitored file descriptors.
        /// @param events Resulted vector of epoll events.
        /// @param max_events Maximum number of events to retrieve.
        /// @param timeout_ms Timeout in milliseconds (-1 = indefinite).
        /// @return An error_code on failure, or void on success.
        [[nodiscard]] expected_void_t wait_events(std::vector<epoll_event>& events, const int max_events,
                                                  const int timeout_ms = -1) noexcept;

        /// @brief Wait for events on monitored file descriptors.
        /// @param max_events Maximum number of events to retrieve.
        /// @param timeout_ms Timeout in milliseconds (-1 = indefinite).
        /// @return Vector of epoll_event on success, or an error_code on failure.
        [[nodiscard]] std::expected<std::vector<epoll_event>, std::error_code> wait_events(const int max_events,
                                                                                           const int timeout_ms = -1) noexcept;
    };
} // namespace kmx::aio::descriptor
