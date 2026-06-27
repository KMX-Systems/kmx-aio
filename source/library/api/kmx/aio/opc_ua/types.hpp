/// @file aio/opc_ua/types.hpp
/// @brief Backend-neutral configuration and statistics types for OPC UA support.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <chrono>
    #include <cstddef>
    #include <cstdint>
    #include <string>
    #include <vector>
#endif

namespace kmx::aio::opc_ua
{
    /// @brief Transport/message security policy used by client/server facades.
    enum class security_mode : std::uint8_t
    {
        /// @brief No signing or encryption.
        none,
        /// @brief Sign messages without encrypting payloads.
        sign,
        /// @brief Sign and encrypt all secure channel messages.
        sign_and_encrypt,
    };

    /// @brief Configuration used to construct an OPC UA client facade.
    struct client_config
    {
        /// @brief OPC UA endpoint URL (for example, @c opc.tcp://127.0.0.1:4840).
        std::string endpoint_url;
        /// @brief Path to client certificate in DER/PEM format when security is enabled.
        std::string certificate_path;
        /// @brief Path to private key matching @ref certificate_path.
        std::string private_key_path;
        /// @brief Path to trusted CA/server certificates store.
        std::string trust_list_path;
        /// @brief Path to certificate revocation list (CRL) store.
        std::string revocation_list_path;
        /// @brief Timeout for the initial connect handshake.
        std::chrono::milliseconds connect_timeout {5000};
        /// @brief Default iterate poll interval used by higher-level loops.
        std::chrono::milliseconds iterate_timeout {50};
        /// @brief Delay before retrying after a failed connection/activation.
        std::chrono::milliseconds reconnect_delay {1000};
        /// @brief Maximum reconnect attempts before surfacing a failure.
        std::uint32_t max_reconnect_attempts = 10u;
        /// @brief Channel/session security mode.
        security_mode mode = security_mode::sign_and_encrypt;
    };

    /// @brief Configuration used to construct an OPC UA server facade.
    struct server_config
    {
        /// @brief TCP listen port for OPC UA endpoint exposure.
        std::uint16_t port = 4840u;
        /// @brief Server application URI used during endpoint/session negotiation.
        std::string application_uri;
        /// @brief Path to server certificate in DER/PEM format when security is enabled.
        std::string certificate_path;
        /// @brief Path to private key matching @ref certificate_path.
        std::string private_key_path;
        /// @brief Path to trusted client certificate store.
        std::string trust_list_path;
        /// @brief Path to certificate revocation list (CRL) store.
        std::string revocation_list_path;
        /// @brief Default iterate cadence for the backend runtime.
        std::chrono::milliseconds iterate_timeout {50};
        /// @brief Maximum simultaneously active sessions accepted by the server.
        std::uint32_t max_sessions = 100u;
        /// @brief Channel/session security mode.
        security_mode mode = security_mode::sign_and_encrypt;
    };

    /// @brief Configuration for monitored-item subscriptions.
    struct subscription_config
    {
        /// @brief Monitored node ids represented by this stream.
        std::vector<std::string> monitored_node_ids;
        /// @brief Publish cadence used by wrapper-side iterate/heartbeat scheduling.
        double publishing_interval_ms = 500.0;
        /// @brief Number of publishing intervals before server considers subscription expired.
        std::uint32_t lifetime_count = 10u;
        /// @brief Maximum keepalive intervals between notifications.
        std::uint32_t max_keepalive_count = 3u;
        /// @brief Upper bound of notifications per publish response.
        std::uint32_t max_notifications_per_publish = 1000u;
        /// @brief Internal SPSC queue capacity for notification fan-out.
        std::size_t notification_queue_capacity = 1024u;
        /// @brief Emit synthetic heartbeat payloads when queue is empty and interval elapses.
        bool emit_heartbeat_when_idle = true;
    };

    /// @brief Read operation result payload.
    struct read_result
    {
        /// @brief Node id that was read.
        std::string node_id;
        /// @brief Scalar value converted to string form by the wrapper.
        std::string value;
        /// @brief Source timestamp reported by backend, if available.
        std::chrono::system_clock::time_point source_timestamp {};
    };

    /// @brief Method call result payload.
    struct method_call_result
    {
        /// @brief Object node id used for method invocation context.
        std::string object_node_id;
        /// @brief Method node id that was invoked.
        std::string method_node_id;
        /// @brief Method output arguments converted to string form by the wrapper.
        std::vector<std::string> output_arguments;
    };

    /// @brief Runtime counters collected by client/server/subscription wrappers.
    struct statistics
    {
        /// @brief Number of connect attempts issued.
        std::uint64_t connect_attempts {};
        /// @brief Number of successful channel/session activations.
        std::uint64_t successful_connects {};
        /// @brief Number of reconnect transitions after an established connection.
        std::uint64_t reconnects {};
        /// @brief Number of read service requests submitted.
        std::uint64_t read_requests {};
        /// @brief Number of write service requests submitted.
        std::uint64_t write_requests {};
        /// @brief Number of call service requests submitted.
        std::uint64_t call_requests {};
        /// @brief Number of notifications delivered to consumers.
        std::uint64_t delivered_notifications {};
        /// @brief Number of notifications dropped due to queue pressure/backpressure.
        std::uint64_t dropped_notifications {};
        /// @brief Number of PubSub messages received by the wrapper.
        std::uint64_t received_pubsub_messages {};
        /// @brief Number of PubSub messages sent by the wrapper.
        std::uint64_t sent_pubsub_messages {};
        /// @brief Number of certificate validation failures.
        std::uint64_t certificate_validation_failures {};
    };

} // namespace kmx::aio::opc_ua
