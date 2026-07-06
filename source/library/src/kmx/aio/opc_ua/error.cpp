/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/opc_ua/error.hpp>

#include <string>

namespace kmx::aio::opc_ua
{
    namespace
    {
        class opc_ua_error_category final: public std::error_category
        {
        public:
            const char* name() const noexcept override { return "opc_ua"; }

            std::string message(const int ev) const override
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
        };

        const opc_ua_error_category opc_ua_error_category_instance {};
    }

    const std::error_category& error_category() noexcept
    {
        return opc_ua_error_category_instance;
    }

    std::error_code make_error_code(const error code) noexcept
    {
        return {static_cast<int>(code), error_category()};
    }

} // namespace kmx::aio::opc_ua
