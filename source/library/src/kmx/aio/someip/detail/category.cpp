/// @file src/kmx/aio/someip/detail/category.cpp
/// @brief The SOME/IP error category messages: the message text of each SOME/IP error.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/someip/detail/category.hpp>
#ifndef PCH
    #include <kmx/aio/someip/error.hpp>

    #include <string>
    #include <string_view>
#endif

namespace kmx::aio::someip::detail
{
    /// @brief Names the errors about this endpoint's own runtime and configuration.
    /// @param value The error to name.
    /// @return Its text, or nothing when it belongs to another group.
    [[nodiscard]] static constexpr std::string_view lifecycle_message(const error value) noexcept
    {
        switch (value)
        {
            case error::success:
                return "success";
            case error::feature_disabled:
                return "SOME/IP feature is disabled";
            case error::not_initialized:
                return "SOME/IP object is not initialized";
            case error::invalid_configuration:
                return "SOME/IP configuration is invalid";
            case error::start_failed:
                return "SOME/IP runtime start failed";
            case error::stopped:
                return "SOME/IP runtime is stopped";
            case error::internal_error:
                return "SOME/IP internal error";
            default:
                return {};
        }
    }

    /// @brief Names the errors about reaching a service and exchanging messages with it.
    /// @param value The error to name.
    /// @return Its text, or nothing when it belongs to another group.
    [[nodiscard]] static constexpr std::string_view service_message(const error value) noexcept
    {
        switch (value)
        {
            case error::service_not_found:
                return "SOME/IP service not found";
            case error::service_unavailable:
                return "SOME/IP service unavailable";
            case error::request_failed:
                return "SOME/IP request failed";
            case error::response_failed:
                return "SOME/IP response failed";
            case error::subscription_closed:
                return "SOME/IP subscription is closed";
            case error::timed_out:
                return "SOME/IP operation timed out";
            default:
                return {};
        }
    }

    std::string category::message(const int ev) const
    {
        const auto value = static_cast<error>(ev);
        if (const auto text = lifecycle_message(value); !text.empty())
            return std::string {text};
        if (const auto text = service_message(value); !text.empty())
            return std::string {text};
        return "unknown SOME/IP error";
    }
}
