/// @file inc/kmx/aio/someip/vsomeip_compat/client_runtime.hpp
/// @brief Backend runtime of a SOME/IP client application, over vsomeip or an in-process stub.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/someip/types.hpp>
    #include <kmx/aio/someip/vsomeip_compat.hpp>

    #include <chrono>
    #include <cstdint>
    #include <memory>
    #include <optional>
    #include <string>
    #include <vector>
#endif

namespace kmx::aio::someip::vsomeip_compat
{
    /// @brief Arguments of one @ref client_runtime::call_method invocation.
    struct call_method_params
    {
        /// @brief Target service identifier.
        service_id_t service_id {};

        /// @brief Target instance identifier.
        instance_id_t instance_id {};

        /// @brief Method to invoke.
        method_id_t method_id {};

        /// @brief Request payload bytes.
        std::vector<std::uint8_t> payload {};

        /// @brief Maximum time to wait for the response.
        std::chrono::milliseconds timeout {};
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
        /// @brief Stops the dispatch thread and releases the vsomeip application.
        ~client_runtime();

        /// @brief Non-copyable: the runtime owns the vsomeip application.
        client_runtime(const client_runtime&) = delete;
        /// @brief Non-copyable: the runtime owns the vsomeip application.
        client_runtime& operator=(const client_runtime&) = delete;
        /// @brief Move constructor — transfers ownership of the vsomeip application.
        client_runtime(client_runtime&&) noexcept;
        /// @brief Move assignment — transfers ownership of the vsomeip application.
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
        /// @param params Target service, instance and method, the request payload, and the maximum wait duration.
        /// @return The response message, or @c std::nullopt on timeout.
        [[nodiscard]] std::optional<rpc_message> call_method(call_method_params params);

        /// @brief Registers event identifiers and subscribes to the event group.
        /// @param config Service, instance, event group and events to subscribe to; its
        ///               @c notification_queue_capacity is the maximum number of notifications to buffer.
        /// @return @c true on success, @c false if not started.
        [[nodiscard]] bool subscribe(const subscription_config& config);

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
        /// @param notification The notification to queue.
        /// @warning Only available in stub builds (no real vsomeip headers).
        void test_push_event(event_notification notification);
#endif

    private:
        /// @brief The backend implementation, real vsomeip or in-process stub.
        struct impl;
        /// @brief The backend implementation, kept opaque so this header need not include vsomeip.
        std::unique_ptr<impl> impl_;
    };

}
