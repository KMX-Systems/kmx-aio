/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/modbus/error.hpp>

#include <string>

namespace kmx::aio::modbus
{
    namespace
    {
        class modbus_error_category final: public std::error_category
        {
        public:
            const char* name() const noexcept override { return "modbus"; }

            std::string message(const int ev) const override
            {
                switch (static_cast<error>(ev))
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
                    case error::tls_handshake_failed:
                        return "Modbus TLS handshake failed";
                    case error::timed_out:
                        return "Modbus operation timed out";
                    case error::internal_error:
                        return "Modbus internal error";
                }

                return "unknown Modbus error";
            }
        };

        const modbus_error_category modbus_error_category_instance {};
    }

    const std::error_category& error_category() noexcept
    {
        return modbus_error_category_instance;
    }

    std::error_code make_error_code(const error code) noexcept
    {
        return {static_cast<int>(code), error_category()};
    }

} // namespace kmx::aio::modbus
