/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/error.hpp>

#include <string>

namespace kmx::aio::knx
{
    namespace detail
    {
        class knx_error_category final: public std::error_category
        {
        public:
            const char* name() const noexcept override { return "knx"; }

            std::string message(const int ev) const override
            {
                switch (static_cast<error>(ev))
                {
                    case error::success:
                        return "success";
                    case error::feature_disabled:
                        return "KNX feature is disabled";
                    case error::invalid_configuration:
                        return "KNX configuration is invalid";
                    case error::malformed_frame:
                        return "KNX frame is malformed or truncated";
                    case error::unsupported_service:
                        return "KNX service is unsupported";
                    case error::unsupported_hpai:
                        return "KNX HPAI is unsupported";
                    case error::unsupported_connection_type:
                        return "KNX connection type is unsupported";
                    case error::invalid_length:
                        return "KNX frame length is invalid";
                    case error::connection_failed:
                        return "KNX connection failed";
                    case error::timeout:
                        return "KNX operation timed out";
                    case error::heartbeat_failed:
                        return "KNX heartbeat failure limit reached";
                    case error::inactivity_timeout:
                        return "KNX inactivity timeout";
                    case error::sequence_error:
                        return "KNX sequence number is invalid";
                    case error::send_queue_full:
                        return "KNX send queue is full";
                    case error::shutdown:
                        return "KNX session is shut down";
                    case error::internal_error:
                        return "KNX internal error";
                    case error::invalid_address:
                        return "KNX address is invalid";
                    case error::unsupported_message_code:
                        return "cEMI message code is unsupported";
                    case error::unsupported_apci:
                        return "KNX application service is unsupported";
                    case error::payload_too_large:
                        return "KNX application payload is too large";
                    case error::unsupported_datapoint:
                        return "KNX datapoint type is unsupported";
                    case error::value_out_of_range:
                        return "KNX datapoint value is out of range";
                    case error::secure_unsupported:
                        return "KNX Secure profile or cryptographic operation is unsupported";
                }
                return "unknown KNX error";
            }
        };

        const knx_error_category knx_error_category_instance {};
    }

    const std::error_category& error_category() noexcept
    {
        return detail::knx_error_category_instance;
    }

    std::error_code make_error_code(const error code) noexcept
    {
        return {static_cast<int>(code), error_category()};
    }
}
