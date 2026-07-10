/// @file aio/someip/types.hpp
/// @brief Backend-neutral configuration and payload types for SOME/IP support.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <chrono>
    #include <cstddef>
    #include <cstdint>
    #include <string>
    #include <vector>
#endif

/// @namespace kmx::aio::someip
/// @brief Asynchronous SOME/IP wrapper for the kmx-aio I/O framework.
namespace kmx::aio::someip
{
    /// @brief SOME/IP service identifier (16-bit, as per PRS_SOMEIP_00038).
    using service_id_t = std::uint16_t;

    /// @brief SOME/IP service instance identifier.
    using instance_id_t = std::uint16_t;

    /// @brief SOME/IP method identifier.
    using method_id_t = std::uint16_t;

    /// @brief SOME/IP event identifier.
    using event_id_t = std::uint16_t;

    /// @brief SOME/IP event group identifier.
    using event_group_id_t = std::uint16_t;

    /// @brief Composite SOME/IP request identifier (client-id | session-id).
    using request_id_t = std::uint32_t;

    /// @brief Configuration for a SOME/IP client application.
    struct client_config
    {
        /// @brief Name of the vsomeip application instance (must be unique per process).
        std::string application_name;

        /// @brief Path to a vsomeip JSON configuration file, or empty to use the default.
        std::string config_file_path;

        /// @brief Primary service identifier the client will communicate with.
        service_id_t service_id {};

        /// @brief Primary instance identifier the client will communicate with.
        instance_id_t instance_id {};

        /// @brief Maximum time to wait for a method-call response.
        std::chrono::milliseconds connect_timeout {5000};

        /// @brief Poll interval used in iterate() to drive the internal dispatch loop.
        std::chrono::milliseconds iterate_timeout {50};

        /// @brief Delay between consecutive reconnect attempts.
        std::chrono::milliseconds reconnect_delay {1000};

        /// @brief Maximum number of reconnect attempts before giving up.
        std::uint32_t max_reconnect_attempts = 10u;
    };

    /// @brief Configuration for a SOME/IP server application.
    struct server_config
    {
        /// @brief Name of the vsomeip application instance (must be unique per process).
        std::string application_name;

        /// @brief Path to a vsomeip JSON configuration file, or empty to use the default.
        std::string config_file_path;

        /// @brief Primary service identifier the server will offer.
        service_id_t service_id {};

        /// @brief Primary instance identifier the server will offer.
        instance_id_t instance_id {};

        /// @brief Poll interval used in iterate() to drive the internal dispatch loop.
        std::chrono::milliseconds iterate_timeout {50};
    };

    /// @brief Configuration for a SOME/IP event subscription.
    struct subscription_config
    {
        /// @brief Service identifier of the publisher.
        service_id_t service_id {};

        /// @brief Instance identifier of the publisher.
        instance_id_t instance_id {};

        /// @brief Event group to subscribe to.
        event_group_id_t event_group_id {};

        /// @brief Individual event identifiers within the event group to observe.
        std::vector<event_id_t> event_ids;

        /// @brief Maximum number of unread notifications buffered before oldest are dropped.
        /// @note Set to @c 0 to discard all incoming notifications (monitoring dropped_events only).
        std::size_t notification_queue_capacity = 1024u;

        /// @brief Maximum wait time in next() before returning @c timed_out.
        std::chrono::milliseconds iterate_timeout {50};
    };

    /// @brief Result returned by a successful method call.
    struct call_result
    {
        /// @brief Service identifier echoed from the response.
        service_id_t service_id {};

        /// @brief Instance identifier echoed from the response.
        instance_id_t instance_id {};

        /// @brief Method identifier echoed from the response.
        method_id_t method_id {};

        /// @brief Response payload bytes.
        std::vector<std::uint8_t> payload;
    };

    /// @brief An incoming method request received by a SOME/IP server.
    struct method_request
    {
        /// @brief Composite request identifier used to correlate send_response() calls.
        request_id_t request_id {};

        /// @brief Service identifier of the requesting client.
        service_id_t service_id {};

        /// @brief Instance identifier of the requesting client.
        instance_id_t instance_id {};

        /// @brief Called method identifier.
        method_id_t method_id {};

        /// @brief Request payload bytes.
        std::vector<std::uint8_t> payload;
    };

    /// @brief A single SOME/IP event notification delivered to a subscriber.
    struct event_notification
    {
        /// @brief Service that generated the event.
        service_id_t service_id {};

        /// @brief Instance that generated the event.
        instance_id_t instance_id {};

        /// @brief Event identifier within the service.
        event_id_t event_id {};

        /// @brief Notification payload bytes.
        std::vector<std::uint8_t> payload;

        /// @brief Timestamp assigned when the notification was received from the transport.
        std::chrono::system_clock::time_point source_timestamp {};
    };

    /// @brief Operational counters exposed by client and server objects.
    struct statistics
    {
        /// @brief Total number of start() invocations.
        std::uint64_t start_attempts {};

        /// @brief Number of start() invocations that succeeded.
        std::uint64_t successful_starts {};

        /// @brief Number of automatic reconnect attempts.
        std::uint64_t reconnects {};

        /// @brief Number of call_method() requests initiated.
        std::uint64_t call_requests {};

        /// @brief Number of requests/responses sent to the transport layer.
        std::uint64_t calls_sent {};

        /// @brief Number of responses/requests received from the transport layer.
        std::uint64_t calls_received {};

        /// @brief Number of event notifications sent (server side).
        std::uint64_t events_sent {};

        /// @brief Number of event notifications received (client/subscription side).
        std::uint64_t events_received {};

        /// @brief Number of notifications discarded due to a full queue (backpressure).
        std::uint64_t dropped_events {};
    };

} // namespace kmx::aio::someip
