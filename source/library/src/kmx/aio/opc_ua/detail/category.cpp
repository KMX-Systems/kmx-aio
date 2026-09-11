/// @file src/kmx/aio/opc_ua/detail/category.cpp
/// @brief The OPC UA error category messages: the message text of each OPC UA error.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/opc_ua/detail/category.hpp>
#ifndef PCH
    #include <kmx/aio/opc_ua/error.hpp>

    #include <string>
#endif

namespace kmx::aio::opc_ua::detail
{
    std::string category::message(const int ev) const
    {
        switch (static_cast<error>(ev))
        {
            case error::success:
                return "success";
            case error::feature_disabled:
                return "OPC UA feature is disabled";
            case error::not_initialized:
                return "OPC UA object is not initialized";
            case error::invalid_configuration:
                return "OPC UA configuration is invalid";
            case error::connect_failed:
                return "OPC UA connection failed";
            case error::disconnected:
                return "OPC UA peer is disconnected";
            case error::request_failed:
                return "OPC UA request failed";
            case error::subscription_closed:
                return "OPC UA subscription is closed";
            case error::security_error:
                return "OPC UA security validation failed";
            case error::timed_out:
                return "OPC UA operation timed out";
            case error::internal_error:
                return "OPC UA internal error";
        }

        return "unknown OPC UA error";
    }
}
