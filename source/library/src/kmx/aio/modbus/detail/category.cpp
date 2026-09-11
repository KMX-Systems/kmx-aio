/// @file src/kmx/aio/modbus/detail/category.cpp
/// @brief The Modbus error category messages: the message text of each Modbus error.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/modbus/detail/category.hpp>
#ifndef PCH
    #include <kmx/aio/modbus/error.hpp>

    #include <string>
    #include <string_view>
#endif

namespace kmx::aio::modbus::detail
{
    /// @brief Names the errors about the link this Modbus session runs over.
    /// @param value The error to name.
    /// @return Its text, or nothing when it belongs to another group.
    [[nodiscard]] static constexpr std::string_view link_message(const error value) noexcept
    {
        switch (value)
        {
            case error::success:
                return "success";
            case error::feature_disabled:
                return "Modbus feature is disabled";
            case error::invalid_configuration:
                return "Modbus configuration is invalid";
            case error::connection_failed:
                return "Modbus connection failed";
            case error::disconnected:
                return "Modbus peer is disconnected";
            case error::tls_handshake_failed:
                return "Modbus TLS handshake failed";
            case error::timed_out:
                return "Modbus operation timed out";
            case error::internal_error:
                return "Modbus internal error";
            default:
                return {};
        }
    }

    /// @brief Names the errors about what a Modbus peer actually said.
    /// @param value The error to name.
    /// @return Its text, or nothing when it belongs to another group.
    [[nodiscard]] static constexpr std::string_view protocol_message(const error value) noexcept
    {
        switch (value)
        {
            case error::exception_response:
                return "Modbus server returned an exception response";
            case error::unexpected_function_code:
                return "Modbus response function code does not match request";
            case error::unexpected_transaction_id:
                return "Modbus response transaction identifier does not match request";
            case error::frame_too_large:
                return "Modbus request PDU exceeds the 253-byte protocol limit";
            case error::malformed_frame:
                return "Modbus frame is malformed or truncated";
            case error::invalid_unit_id:
                return "Modbus response unit identifier does not match request";
            default:
                return {};
        }
    }

    std::string category::message(const int ev) const
    {
        const auto value = static_cast<error>(ev);
        if (const auto text = link_message(value); !text.empty())
            return std::string {text};
        if (const auto text = protocol_message(value); !text.empty())
            return std::string {text};
        return "unknown Modbus error";
    }
}
