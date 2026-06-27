/// @file aio/opc_ua/error.hpp
/// @brief OPC UA-specific error domain for wrapper-level failures.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <system_error>
#endif

namespace kmx::aio::opc_ua
{
    /// @brief Wrapper-level OPC UA error codes.
    enum class error : std::uint32_t
    {
        /// @brief No error.
        success = 0u,
        /// @brief OPC UA feature/backend support is disabled at build time.
        feature_disabled,
        /// @brief Object/backend is not initialized.
        not_initialized,
        /// @brief Provided configuration is invalid/incomplete.
        invalid_configuration,
        /// @brief Connection or session establishment failed.
        connect_failed,
        /// @brief Operation requires active connection/session.
        disconnected,
        /// @brief Service request failed with backend-specific status.
        request_failed,
        /// @brief Subscription was closed or is not open.
        subscription_closed,
        /// @brief Certificate, trust, or secure-channel failure.
        security_error,
        /// @brief Operation timed out.
        timed_out,
        /// @brief Internal wrapper/runtime failure.
        internal_error,
    };

    /// @brief Access the OPC UA wrapper error category singleton.
    /// @return Error category used for @ref error values.
    [[nodiscard]] const std::error_category& error_category() noexcept;
    /// @brief Convert wrapper error enum to `std::error_code`.
    /// @param code Wrapper error enum value.
    /// @return `std::error_code` bound to @ref error_category.
    [[nodiscard]] std::error_code make_error_code(error code) noexcept;

} // namespace kmx::aio::opc_ua

namespace std
{
    template <>
    struct is_error_code_enum<kmx::aio::opc_ua::error>: true_type
    {
    };
}
