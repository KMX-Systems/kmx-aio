/// @file aio/someip/error.hpp
/// @brief SOME/IP-specific error domain for wrapper-level failures.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <system_error>
#endif

namespace kmx::aio::someip
{
    /// @brief Error codes produced by the SOME/IP wrapper layer.
    /// @note Values of this enum integrate with @c std::error_code via the
    ///       @c std::is_error_code_enum specialization below.
    enum class error : std::uint32_t
    {
        success = 0u,          ///< Operation completed successfully.
        feature_disabled,      ///< SOME/IP feature gate is disabled at build time.
        not_initialized,       ///< Object has not been started yet.
        invalid_configuration, ///< A required configuration field is missing or invalid.
        start_failed,          ///< Runtime failed to initialise or register with the router.
        stopped,               ///< Operation rejected because the runtime is already stopped.
        service_not_found,     ///< Service discovery returned no matching service entry.
        service_unavailable,   ///< Service exists but is currently unreachable.
        request_failed,        ///< A request/offer operation was rejected by the backend.
        response_failed,       ///< Sending a response back to the caller failed.
        subscription_closed,   ///< The subscription is not open.
        timed_out,             ///< Operation exceeded the configured timeout.
        internal_error,        ///< Unexpected internal backend error.
    };

    /// @brief Returns the SOME/IP error category singleton.
    /// @return A reference to the @c std::error_category for @c kmx::aio::someip::error.
    [[nodiscard]] const std::error_category& error_category() noexcept;

    /// @brief Constructs a @c std::error_code from a SOME/IP @c error value.
    /// @param code The error enumerator to convert.
    /// @return An @c std::error_code backed by @c error_category().
    [[nodiscard]] std::error_code make_error_code(error code) noexcept;

} // namespace kmx::aio::someip

namespace std
{
    /// @brief Enables implicit conversion of @c kmx::aio::someip::error to @c std::error_code.
    template <>
    struct is_error_code_enum<kmx::aio::someip::error>: true_type
    {
    };
}
