/// @file aio/opc_ua/subscription.hpp
/// @brief Backend-neutral subscription facade for OPC UA monitored items.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <expected>
    #include <memory>
    #include <string>
    #include <system_error>

    #include <kmx/aio/opc_ua/types.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::opc_ua
{
    class client;

    /// @brief Notification emitted by subscription streams.
    struct notification
    {
        /// @brief Node id producing this notification.
        std::string node_id;
        /// @brief Notification payload converted to wrapper string form.
        std::string payload;
        /// @brief Source timestamp supplied by backend when available.
        std::chrono::system_clock::time_point source_timestamp {};
    };

    /// @brief Backend-neutral asynchronous subscription facade.
    /// @details
    /// `subscription` can be constructed with or without a bound client. Once bound,
    /// callers can open/close and consume notifications via coroutine-based operations.
    class subscription
    {
    public:
        /// @brief Construct an unbound subscription configuration.
        /// @param config Subscription/queue timing and node list settings.
        explicit subscription(subscription_config config) noexcept;
        /// @brief Construct and bind to an existing client.
        /// @param bound_client Client used for backend subscription operations.
        /// @param config Subscription/queue timing and node list settings.
        subscription(client& bound_client, subscription_config config) noexcept;
        /// @brief Destroy subscription and release internal resources.
        ~subscription() noexcept;

        subscription(const subscription&) = delete;
        subscription& operator=(const subscription&) = delete;
        subscription(subscription&&) noexcept;
        subscription& operator=(subscription&&) noexcept;

        /// @brief Open backend subscription resources asynchronously.
        /// @return Task resolving to success or error.
        [[nodiscard]] task<std::expected<void, std::error_code>> open() noexcept(false);
        /// @brief Close backend subscription resources asynchronously.
        /// @return Task resolving to success or error.
        [[nodiscard]] task<std::expected<void, std::error_code>> close() noexcept(false);
        /// @brief Await the next notification from internal queue/stream.
        /// @return Task resolving to notification payload or error.
        [[nodiscard]] task<std::expected<notification, std::error_code>> next() noexcept(false);
        /// @brief Bind this subscription to a client after construction.
        /// @param bound_client Client used for backend subscription operations.
        /// @return Success or error when binding is invalid/unavailable.
        [[nodiscard]] std::expected<void, std::error_code> bind(client& bound_client) noexcept;

        /// @brief Access immutable subscription configuration.
        /// @return Reference to active configuration.
        [[nodiscard]] const subscription_config& config() const noexcept;

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };

} // namespace kmx::aio::opc_ua
