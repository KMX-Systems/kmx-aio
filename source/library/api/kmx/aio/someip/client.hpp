/// @file aio/someip/client.hpp
/// @brief Backend-neutral async SOME/IP client facade.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <chrono>
    #include <cstdint>
    #include <expected>
    #include <memory>
    #include <system_error>
    #include <vector>

    #include <kmx/aio/someip/error.hpp>
    #include <kmx/aio/someip/types.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::someip
{
    /// @brief Backend-neutral coroutine-based SOME/IP client.
    ///
    /// Wraps vsomeip (or a configurable stub) behind a uniform coroutine API.
    /// All async operations suspend the calling coroutine and resume on the
    /// associated executor without blocking threads.
    ///
    /// @note A single @c client instance must not be used concurrently from
    ///       multiple coroutines without external synchronization.
    class client
    {
    public:
        /// @brief Constructs a client with the given configuration.
        /// @param config Application name, service coordinates, and timeout settings.
        explicit client(client_config config) noexcept;
        ~client() noexcept;

        client(const client&) = delete;
        client& operator=(const client&) = delete;
        client(client&&) noexcept;
        client& operator=(client&&) noexcept;

        /// @brief Initialises the vsomeip runtime and registers the application.
        /// @return void on success, or an error code on failure.
        /// @throws std::bad_alloc if runtime allocation fails.
        [[nodiscard]] task<std::expected<void, std::error_code>> start() noexcept(false);

        /// @brief Stops the runtime and deregisters the application.
        /// @return void on success, or @c error::stopped if already stopped.
        [[nodiscard]] task<std::expected<void, std::error_code>> stop() noexcept(false);

        /// @brief Drives the internal dispatch loop for one time slot.
        /// @param timeout Maximum time to wait for pending events.
        /// @return @c true while the runtime is active, @c false after stop().
        [[nodiscard]] task<std::expected<bool, std::error_code>> iterate(std::chrono::milliseconds timeout) noexcept(false);

        /// @brief Requests service discovery for a specific service/instance pair.
        /// @param service_id Service identifier to track.
        /// @param instance_id Instance identifier to track.
        /// @return void on success, or an error code on failure.
        [[nodiscard]] task<std::expected<void, std::error_code>> request_service(service_id_t service_id,
                                                                                 instance_id_t instance_id) noexcept(false);

        /// @brief Releases a previously requested service/instance pair.
        /// @param service_id Service identifier to release.
        /// @param instance_id Instance identifier to release.
        /// @return void on success, or an error code on failure.
        [[nodiscard]] task<std::expected<void, std::error_code>> release_service(service_id_t service_id,
                                                                                 instance_id_t instance_id) noexcept(false);

        /// @brief Invokes a remote SOME/IP method and awaits the response.
        /// @param service_id  Target service identifier.
        /// @param instance_id Target instance identifier.
        /// @param method_id   Method to invoke.
        /// @param payload     Request payload bytes.
        /// @return Call result with echoed IDs and response payload, or an error code.
        /// @note Blocks (suspends) the coroutine for up to @c client_config::connect_timeout.
        [[nodiscard]] task<std::expected<call_result, std::error_code>> call_method(service_id_t service_id, instance_id_t instance_id,
                                                                                    method_id_t method_id,
                                                                                    std::vector<std::uint8_t> payload) noexcept(false);

        /// @brief Returns whether a specific service/instance is currently reachable.
        /// @param service_id  Service to query.
        /// @param instance_id Instance to query.
        /// @return @c true if the service is available, @c false otherwise.
        [[nodiscard]] bool is_service_available(service_id_t service_id, instance_id_t instance_id) const noexcept;

        /// @brief Returns the configuration supplied at construction.
        [[nodiscard]] const client_config& config() const noexcept;

        /// @brief Returns a snapshot of operational statistics.
        /// @note @c statistics::dropped_events is synchronised from the backend on each call.
        [[nodiscard]] const statistics& get_stats() const noexcept;

#if !defined(KMX_AIO_HAS_VSOMEIP_HEADER)
        /// @brief Test-only: marks a service/instance as available without network interaction.
        /// @warning Available only in stub builds (no real vsomeip headers).
        void __kmx_test_inject_service_available(service_id_t service_id, instance_id_t instance_id) noexcept;

        /// @brief Test-only: overrides the return status of the next call_method() invocation.
        /// @param status  Zero for success; non-zero causes @c error::request_failed.
        /// @warning Available only in stub builds (no real vsomeip headers).
        void __kmx_test_set_next_call_status(std::uint32_t status) noexcept;
#endif

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };

} // namespace kmx::aio::someip
