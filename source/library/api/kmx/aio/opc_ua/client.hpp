/// @file aio/opc_ua/client.hpp
/// @brief Backend-neutral async OPC UA client facade.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <chrono>
    #include <expected>
    #include <memory>
    #include <cstdint>
    #include <system_error>
    #include <vector>

    #include <kmx/aio/opc_ua/error.hpp>
    #include <kmx/aio/opc_ua/types.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::opc_ua
{
    /// @brief Backend-neutral asynchronous OPC UA client facade.
    /// @details
    /// The client presents coroutine-friendly operations (`connect`, `iterate`, `read_node`,
    /// `write_node`, `call_method`) and delegates transport/backend details to either
    /// open62541 (feature-enabled) or an internal shim (feature-disabled/test mode).
    class client
    {
    public:
        /// @brief Construct a client with the provided runtime configuration.
        /// @param config Client endpoint/security/retry configuration.
        explicit client(client_config config) noexcept;
        /// @brief Destroy the client and release backend resources.
        ~client() noexcept;

        client(const client&) = delete;
        client& operator=(const client&) = delete;
        client(client&&) noexcept;
        client& operator=(client&&) noexcept;

        /// @brief Begin asynchronous connect/session activation.
        /// @return Task resolving to `std::expected<void, std::error_code>`.
        /// Success indicates the session is established and usable for service requests.
        [[nodiscard]] task<std::expected<void, std::error_code>> connect() noexcept(false);
        /// @brief Begin asynchronous disconnect/session close.
        /// @return Task resolving to `std::expected<void, std::error_code>`.
        [[nodiscard]] task<std::expected<void, std::error_code>> disconnect() noexcept(false);
        /// @brief Drive backend progress for in-flight operations.
        /// @param timeout Backend-dependent iterate/poll timeout.
        /// @return Task resolving to `std::expected<bool, std::error_code>`.
        /// A `true` value indicates progress/continued activity, while `false` indicates no work.
        [[nodiscard]] task<std::expected<bool, std::error_code>> iterate(std::chrono::milliseconds timeout) noexcept(false);
        /// @brief Submit asynchronous read request for a single node.
        /// @param node_id OPC UA node id string (for example, `ns=2;s=Demo.Static.Scalar.String`).
        /// @return Task resolving to read result payload or error.
        [[nodiscard]] task<std::expected<read_result, std::error_code>> read_node(std::string node_id) noexcept(false);
        /// @brief Submit asynchronous write request for a single node.
        /// @param node_id OPC UA node id string.
        /// @param value Value converted and forwarded to backend.
        /// @return Task resolving to success or error.
        [[nodiscard]] task<std::expected<void, std::error_code>> write_node(std::string node_id, std::string value) noexcept(false);
        /// @brief Submit asynchronous method call request.
        /// @param object_node_id OPC UA object node id used as call context.
        /// @param method_node_id OPC UA method node id to invoke.
        /// @param input_arguments Method inputs represented as strings.
        /// @return Task resolving to method call output payload or error.
        [[nodiscard]] task<std::expected<method_call_result, std::error_code>>
        call_method(std::string object_node_id, std::string method_node_id, std::vector<std::string> input_arguments) noexcept(false);
        /// @brief Check whether the client currently has an activated session.
        /// @return `true` when channel/session state is active.
        [[nodiscard]] bool has_active_session() const noexcept;

        /// @brief Access immutable construction/runtime configuration.
        /// @return Reference to active client configuration.
        [[nodiscard]] const client_config& config() const noexcept;
        /// @brief Access runtime statistics counters.
        /// @return Reference to cumulative statistics snapshot.
        [[nodiscard]] const statistics& get_stats() const noexcept;

    #if !defined(KMX_AIO_FEATURE_OPC_UA)
        /// @brief Test-only hook to inject one-shot status codes for next shim requests.
        /// @param read_status Status returned for next read callback.
        /// @param write_status Status returned for next write callback.
        /// @param call_status Status returned for next call callback.
        void __kmx_test_set_next_request_statuses(std::uint32_t read_status,
                              std::uint32_t write_status,
                              std::uint32_t call_status) noexcept;
    #endif

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };

} // namespace kmx::aio::opc_ua
