/// @file aio/someip/vsomeip_compat.hpp
/// @brief Internal backend abstraction layer between the SOME/IP facade and vsomeip.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <kmx/aio/someip/types.hpp>

#if defined(KMX_AIO_FEATURE_SOMEIP) && defined(KMX_AIO_SOMEIP_LINK_BACKEND)
    #if __has_include(<vsomeip/vsomeip.hpp>)
        #define KMX_AIO_HAS_VSOMEIP_HEADER 1
    #elif __has_include(<vsomeip3/vsomeip.hpp>)
        #define KMX_AIO_HAS_VSOMEIP_HEADER 1
    #endif
#endif

#if defined(KMX_AIO_HAS_VSOMEIP_HEADER)
    #if defined(__clang__)
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wunused-parameter"
    #elif defined(__GNUC__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wunused-parameter"
    #endif

    #if __has_include(<vsomeip/vsomeip.hpp>)
        #include <vsomeip/vsomeip.hpp>
    #else
        #include <vsomeip3/vsomeip.hpp>
    #endif

    #if defined(__clang__)
        #pragma clang diagnostic pop
    #elif defined(__GNUC__)
        #pragma GCC diagnostic pop
    #endif
#endif

/// @namespace kmx::aio::someip::compat
/// @brief Internal backend abstraction; not part of the public SOME/IP API.
namespace kmx::aio::someip::compat
{
    /// @brief Represents a single SOME/IP RPC message (request or response).
    struct rpc_message
    {
        /// @brief Service identifier.
        service_id_t service_id {};

        /// @brief Instance identifier.
        instance_id_t instance_id {};

        /// @brief Method identifier.
        method_id_t method_id {};

        /// @brief Composite request identifier (client-id | session-id).
        request_id_t request_id {};

        /// @brief Payload bytes.
        std::vector<std::uint8_t> payload;
    };

    /// @brief Backend runtime abstraction for a SOME/IP client application.
    ///
    /// When vsomeip headers are present the real vsomeip application is used;
    /// otherwise a deterministic in-process stub is activated automatically.
    ///
    /// @note Thread-safety: start() and stop() are not thread-safe with respect
    ///       to each other.  All other methods may be called after start() from
    ///       any thread.
    class client_runtime
    {
    public:
        /// @brief Constructs a runtime for the named application.
        /// @param application_name Unique vsomeip application name.
        /// @param config_file_path Path to vsomeip JSON config, or empty for the default.
        client_runtime(std::string application_name, std::string config_file_path);
        ~client_runtime();

        client_runtime(const client_runtime&) = delete;
        client_runtime& operator=(const client_runtime&) = delete;
        client_runtime(client_runtime&&) noexcept;
        client_runtime& operator=(client_runtime&&) noexcept;

        /// @brief Initialises the vsomeip application and starts its dispatch thread.
        /// @return @c true on success, @c false if already started or init failed.
        [[nodiscard]] bool start();

        /// @brief Stops the dispatch thread and releases vsomeip resources.
        /// @return @c true always.
        [[nodiscard]] bool stop() noexcept;

        /// @brief Issues a request_service() to vsomeip for availability tracking.
        /// @return @c true on success, @c false if not started.
        [[nodiscard]] bool request_service(service_id_t service_id, instance_id_t instance_id);

        /// @brief Releases a previously requested service.
        /// @return @c true on success, @c false if not started.
        [[nodiscard]] bool release_service(service_id_t service_id, instance_id_t instance_id);

        /// @brief Returns whether a service/instance pair is currently available.
        [[nodiscard]] bool is_service_available(service_id_t service_id, instance_id_t instance_id) const;

        /// @brief Sends a SOME/IP request and blocks (via condition variable) for the response.
        /// @param timeout Maximum wait duration.
        /// @return The response message, or @c std::nullopt on timeout.
        [[nodiscard]] std::optional<rpc_message> call_method(service_id_t service_id, instance_id_t instance_id, method_id_t method_id,
                                                             std::vector<std::uint8_t> payload, std::chrono::milliseconds timeout);

        /// @brief Registers event identifiers and subscribes to the event group.
        /// @param queue_capacity Maximum number of notifications to buffer.
        /// @return @c true on success, @c false if not started.
        [[nodiscard]] bool subscribe(service_id_t service_id, instance_id_t instance_id, event_group_id_t event_group_id,
                                     const std::vector<event_id_t>& event_ids, std::size_t queue_capacity);

        /// @brief Cancels event registration and unsubscribes from the event group.
        /// @return @c true on success, @c false if not started.
        [[nodiscard]] bool unsubscribe(service_id_t service_id, instance_id_t instance_id, event_group_id_t event_group_id,
                                       const std::vector<event_id_t>& event_ids);

        /// @brief Dequeues the next buffered event notification.
        /// @param timeout Maximum time to wait if the queue is empty.
        /// @return The oldest buffered notification, or @c std::nullopt on timeout.
        [[nodiscard]] std::optional<event_notification> next_event(std::chrono::milliseconds timeout);

        /// @brief Returns the total number of notifications dropped due to a full buffer.
        [[nodiscard]] std::uint64_t dropped_events() const noexcept;

#if !defined(KMX_AIO_HAS_VSOMEIP_HEADER)
        /// @brief Test-only: injects a synthetic notification into the event queue.
        /// @warning Only available in stub builds (no real vsomeip headers).
        void __kmx_test_push_event(event_notification notification);
#endif

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };

    /// @brief Backend runtime abstraction for a SOME/IP server application.
    ///
    /// When vsomeip headers are present the real vsomeip application is used;
    /// otherwise a deterministic in-process stub is activated automatically.
    class server_runtime
    {
    public:
        /// @brief Constructs a runtime for the named application.
        /// @param application_name Unique vsomeip application name.
        /// @param config_file_path Path to vsomeip JSON config, or empty for the default.
        server_runtime(std::string application_name, std::string config_file_path);
        ~server_runtime();

        server_runtime(const server_runtime&) = delete;
        server_runtime& operator=(const server_runtime&) = delete;
        server_runtime(server_runtime&&) noexcept;
        server_runtime& operator=(server_runtime&&) noexcept;

        /// @brief Initialises the vsomeip application and starts its dispatch thread.
        /// @return @c true on success, @c false if already started or init failed.
        [[nodiscard]] bool start();

        /// @brief Stops the dispatch thread and releases vsomeip resources.
        /// @return @c true always.
        [[nodiscard]] bool stop() noexcept;

        /// @brief Advertises a service/instance via vsomeip Service Discovery.
        /// @return @c true on success, @c false if not started.
        [[nodiscard]] bool offer_service(service_id_t service_id, instance_id_t instance_id);

        /// @brief Withdraws a previously offered service from Service Discovery.
        /// @return @c true on success, @c false if not started.
        [[nodiscard]] bool stop_offer_service(service_id_t service_id, instance_id_t instance_id);

        /// @brief Dequeues the oldest pending method request.
        /// @return The request, or @c std::nullopt if none is pending.
        [[nodiscard]] std::optional<rpc_message> next_request();

        /// @brief Sends a response to a previously dequeued request.
        /// @return @c true on success, @c false if the request_id is not tracked.
        [[nodiscard]] bool send_response(request_id_t request_id, std::vector<std::uint8_t> payload);

        /// @brief Publishes an event notification to all active subscribers.
        /// @return @c true on success, @c false if not started.
        [[nodiscard]] bool notify(service_id_t service_id, instance_id_t instance_id, event_id_t event_id,
                                  std::vector<std::uint8_t> payload);

        /// @brief Registers a callback invoked from the vsomeip thread when a request arrives.
        /// @param handler Zero-argument callable; intended for waking a waiting coroutine.
        void set_request_handler(std::function<void()> handler);

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };

} // namespace kmx::aio::someip::compat
