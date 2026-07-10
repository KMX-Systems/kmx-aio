/// @file aio/someip/server.hpp
/// @brief Backend-neutral async SOME/IP server facade.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <chrono>
    #include <expected>
    #include <memory>
    #include <system_error>

    #include <kmx/aio/someip/error.hpp>
    #include <kmx/aio/someip/types.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::someip
{
    /// @brief Backend-neutral coroutine-based SOME/IP server.
    ///
    /// Wraps vsomeip (or a configurable stub) to expose a service that can receive
    /// method requests, send responses, and publish event notifications.
    ///
    /// @note A single @c server instance must not be used concurrently from
    ///       multiple coroutines without external synchronization.
    class server
    {
    public:
        /// @brief Constructs a server with the given configuration.
        /// @param config Application name, service coordinates, and timing settings.
        explicit server(server_config config) noexcept;
        ~server() noexcept;

        server(const server&) = delete;
        server& operator=(const server&) = delete;
        server(server&&) noexcept;
        server& operator=(server&&) noexcept;

        /// @brief Initialises the vsomeip runtime and registers the application.
        /// @return void on success, or an error code on failure.
        /// @throws std::bad_alloc if runtime allocation fails.
        [[nodiscard]] task<std::expected<void, std::error_code>> start() noexcept(false);

        /// @brief Stops the runtime and withdraws all offered services.
        /// @return void on success, or @c error::stopped if already stopped.
        [[nodiscard]] task<std::expected<void, std::error_code>> stop() noexcept(false);

        /// @brief Drives the internal dispatch loop for one time slot.
        /// @param timeout Maximum time to wait for pending events.
        /// @return @c true while the runtime is active, @c false after stop().
        [[nodiscard]] task<std::expected<bool, std::error_code>> iterate(std::chrono::milliseconds timeout) noexcept(false);

        /// @brief Advertises a service/instance to the network via Service Discovery.
        /// @param service_id  Identifier of the service to offer.
        /// @param instance_id Instance of the service to offer.
        /// @return void on success, or an error code on failure.
        [[nodiscard]] task<std::expected<void, std::error_code>> offer_service(service_id_t service_id,
                                                                               instance_id_t instance_id) noexcept(false);

        /// @brief Withdraws a previously advertised service from Service Discovery.
        /// @param service_id  Identifier of the service to withdraw.
        /// @param instance_id Instance of the service to withdraw.
        /// @return void on success, or an error code on failure.
        [[nodiscard]] task<std::expected<void, std::error_code>> stop_offer_service(service_id_t service_id,
                                                                                    instance_id_t instance_id) noexcept(false);

        /// @brief Dequeues the next incoming method request.
        /// @return The oldest pending request, or @c error::timed_out if none is available.
        [[nodiscard]] task<std::expected<method_request, std::error_code>> next_request() noexcept(false);

        /// @brief Sends a response to a previously dequeued method request.
        /// @param request_id Identifier echoed from @c method_request::request_id.
        /// @param payload    Response payload bytes.
        /// @return void on success, or an error code on failure.
        [[nodiscard]] task<std::expected<void, std::error_code>> send_response(request_id_t request_id,
                                                                               std::vector<std::uint8_t> payload) noexcept(false);

        /// @brief Broadcasts an event notification to all active subscribers.
        /// @param service_id  Service that is publishing the event.
        /// @param instance_id Instance that is publishing the event.
        /// @param event_id    Event identifier.
        /// @param payload     Event payload bytes.
        /// @return void on success, or an error code if the service is not offered.
        [[nodiscard]] task<std::expected<void, std::error_code>> notify(service_id_t service_id, instance_id_t instance_id,
                                                                        event_id_t event_id,
                                                                        std::vector<std::uint8_t> payload) noexcept(false);

        /// @brief Returns the configuration supplied at construction.
        [[nodiscard]] const server_config& config() const noexcept;

        /// @brief Returns a snapshot of operational statistics.
        [[nodiscard]] const statistics& get_stats() const noexcept;

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };

} // namespace kmx::aio::someip
