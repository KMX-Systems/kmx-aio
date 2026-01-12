#pragma once
#ifndef PCH
    #include <kmx/aio/descriptor/file.hpp>
#endif

namespace kmx::aio::descriptor
{
    /// @brief RAII wrapper for epoll file descriptors with type-safe operations.
    class epoll: public file
    {
    public:
        using result_t = std::expected<void, std::error_code>;

        epoll() noexcept = default;

        explicit epoll(const fd_t fd) noexcept: file(fd) {}

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
        [[nodiscard]] result_t add_monitored_fd(const fd_t fd, const event_mask_t events = default_epoll_events) noexcept;

        /// @brief Modify the monitored events for a file descriptor.
        /// @param fd The file descriptor to modify.
        /// @param events The new events to monitor (bitmask of epoll_event_mask).
        /// @return An error_code on failure, or void on success.
        [[nodiscard]] result_t modify_events(const fd_t fd, const event_mask_t events) noexcept;

        /// @brief Remove a file descriptor from epoll monitoring.
        /// @param fd The file descriptor to stop monitoring.
        /// @return An error_code on failure, or void on success.
        [[nodiscard]] result_t remove_monitored_fd(const fd_t fd) noexcept;

        /// @brief Wait for events on monitored file descriptors.
        /// @param events Resulted vector of epoll events.
        /// @param max_events Maximum number of events to retrieve.
        /// @param timeout_ms Timeout in milliseconds (-1 = indefinite).
        /// @return An error_code on failure, or void on success.
        [[nodiscard]] result_t wait_events(std::vector<epoll_event>& events, const int max_events, const int timeout_ms = -1) noexcept;

        /// @brief Wait for events on monitored file descriptors.
        /// @param max_events Maximum number of events to retrieve.
        /// @param timeout_ms Timeout in milliseconds (-1 = indefinite).
        /// @return Vector of epoll_event on success, or an error_code on failure.
        [[nodiscard]] std::expected<std::vector<epoll_event>, std::error_code> wait_events(const int max_events,
                                                                                           const int timeout_ms = -1) noexcept;
    };
} // namespace kmx::aio::descriptor
