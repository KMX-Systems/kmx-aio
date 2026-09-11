/// @file inc/kmx/aio/someip/vsomeip_compat/server_runtime.hpp
/// @brief Backend runtime of a SOME/IP server application, over vsomeip or an in-process stub.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/someip/types.hpp>
    #include <kmx/aio/someip/vsomeip_compat.hpp>

    #include <cstdint>
    #include <functional>
    #include <memory>
    #include <optional>
    #include <string>
    #include <vector>
#endif

namespace kmx::aio::someip::vsomeip_compat
{
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
        /// @brief Stops the dispatch thread and releases the vsomeip application.
        ~server_runtime();

        /// @brief Non-copyable: the runtime owns the vsomeip application.
        server_runtime(const server_runtime&) = delete;
        /// @brief Non-copyable: the runtime owns the vsomeip application.
        server_runtime& operator=(const server_runtime&) = delete;
        /// @brief Move constructor — transfers ownership of the vsomeip application.
        server_runtime(server_runtime&&) noexcept;
        /// @brief Move assignment — transfers ownership of the vsomeip application.
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
        /// @brief The backend implementation, real vsomeip or in-process stub.
        struct impl;
        /// @brief The backend implementation, kept opaque so this header need not include vsomeip.
        std::unique_ptr<impl> impl_;
    };

}
