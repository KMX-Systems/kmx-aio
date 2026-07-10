/// @file aio/someip/subscription.hpp
/// @brief Backend-neutral subscription facade for SOME/IP events.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <expected>
    #include <memory>
    #include <system_error>

    #include <kmx/aio/someip/types.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::someip
{
    class client;

    /// @brief Backend-neutral coroutine-based SOME/IP event subscription.
    ///
    /// Manages subscription to an event group on a specific service/instance.
    /// Notifications are buffered internally up to @c subscription_config::notification_queue_capacity;
    /// older notifications are evicted silently when the buffer is full.
    ///
    /// @note A @c subscription must be bound to a @c client before @c open() is called,
    ///       either via the two-argument constructor or via @c bind().
    class subscription
    {
    public:
        /// @brief Constructs an unbound subscription (requires a bind() call before open()).
        /// @param config Event-group coordinates, capacity, and timing settings.
        explicit subscription(subscription_config config) noexcept;

        /// @brief Constructs a subscription pre-bound to an existing client.
        /// @param bound_client The client whose application name and config are reused.
        /// @param config       Event-group coordinates, capacity, and timing settings.
        subscription(client& bound_client, subscription_config config) noexcept;
        ~subscription() noexcept;

        subscription(const subscription&) = delete;
        subscription& operator=(const subscription&) = delete;
        subscription(subscription&&) noexcept;
        subscription& operator=(subscription&&) noexcept;

        /// @brief Starts the subscription runtime and subscribes to the event group.
        /// @return void on success, or an error code on failure.
        /// @note Requires a bound client; returns @c error::invalid_configuration if unbound.
        [[nodiscard]] task<std::expected<void, std::error_code>> open() noexcept(false);

        /// @brief Cancels the subscription and releases backend resources.
        /// @return void on success, or @c error::subscription_closed if already closed.
        [[nodiscard]] task<std::expected<void, std::error_code>> close() noexcept(false);

        /// @brief Waits for and dequeues the next event notification.
        /// @return The oldest buffered notification, or @c error::timed_out if none arrives
        ///         within @c subscription_config::iterate_timeout.
        [[nodiscard]] task<std::expected<event_notification, std::error_code>> next() noexcept(false);

        /// @brief Binds the subscription to a client after construction.
        /// @param bound_client The client to bind to.
        /// @return void on success, or @c error::invalid_configuration if already open.
        [[nodiscard]] std::expected<void, std::error_code> bind(client& bound_client) noexcept;

        /// @brief Returns the configuration supplied at construction.
        [[nodiscard]] const subscription_config& config() const noexcept;

        /// @brief Returns the cumulative number of notifications dropped due to a full queue.
        /// @note The counter is per runtime session; it resets when close() and open() are called.
        [[nodiscard]] std::uint64_t dropped_events() const noexcept;

        // Test-only injection hook – available when the real vsomeip backend is absent.
#if defined(KMX_AIO_FEATURE_SOMEIP) && defined(KMX_AIO_SOMEIP_LINK_BACKEND)
    #if __has_include(<vsomeip/vsomeip.hpp>)
        #define KMX_AIO_HAS_VSOMEIP_HEADER 1
    #elif __has_include(<vsomeip3/vsomeip.hpp>)
        #define KMX_AIO_HAS_VSOMEIP_HEADER 1
    #endif
#endif

#if !defined(KMX_AIO_HAS_VSOMEIP_HEADER)
        /// @brief Test-only: pushes a synthetic event directly into the internal queue.
        /// @param notification Event to inject.
        /// @warning Available only in stub/test builds without real vsomeip headers.
        void __kmx_test_push_event(event_notification notification);
#endif

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };

} // namespace kmx::aio::someip
