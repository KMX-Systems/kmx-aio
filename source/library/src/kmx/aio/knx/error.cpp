/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/error.hpp>

#include <string>
#include <string_view>

namespace kmx::aio::knx
{
    namespace detail
    {
        class knx_error_category final: public std::error_category
        {
        public:
            const char* name() const noexcept override { return "knx"; }

            /// @brief Names the errors raised while reading or building a frame.
            /// @param value The error to name.
            /// @return Its text, or nothing when it belongs to another group.
            [[nodiscard]] static constexpr std::string_view framing_message(const error value) noexcept
            {
                switch (value)
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
                    default:
                        return {};
                }
            }

            /// @copydoc framing_message
            /// @brief Names the errors raised while running a connection.
            [[nodiscard]] static constexpr std::string_view session_message(const error value) noexcept
            {
                switch (value)
                {
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
                    default:
                        return {};
                }
            }

            /// @copydoc framing_message
            /// @brief Names the errors raised by the application layer.
            [[nodiscard]] static constexpr std::string_view application_message(const error value) noexcept
            {
                switch (value)
                {
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
                    default:
                        return {};
                }
            }

            /// @copydoc framing_message
            /// @brief Names the errors raised by KNX Secure.
            /// @note None of these texts names a key, a sequence number or anything else taken from a frame.
            [[nodiscard]] static constexpr std::string_view security_message(const error value) noexcept
            {
                switch (value)
                {
                    case error::secure_unsupported:
                        return "KNX Secure profile or cryptographic operation is unsupported";
                    case error::secure_authentication_failed:
                        return "KNX Secure message authentication failed";
                    case error::secure_replay:
                        return "KNX Secure frame is a replay or outside its acceptance window";
                    case error::secure_session_rejected:
                        return "KNX Secure session authentication was rejected";
                    case error::secure_session_closed:
                        return "KNX Secure session is closed";
                    case error::secure_key_missing:
                        return "KNX Secure key is not configured for this frame";
                    case error::secure_frame_required:
                        return "KNX Secure requires this frame to be secured";
                    case error::keyring_signature_invalid:
                        return "KNX keyring signature is invalid or the password is wrong";
                    case error::crypto_failure:
                        return "KNX cryptographic backend failure";
                    default:
                        return {};
                }
            }

            std::string message(const int ev) const override
            {
                const auto value = static_cast<error>(ev);
                if (const auto text = framing_message(value); !text.empty())
                    return std::string {text};
                if (const auto text = session_message(value); !text.empty())
                    return std::string {text};
                if (const auto text = application_message(value); !text.empty())
                    return std::string {text};
                if (const auto text = security_message(value); !text.empty())
                    return std::string {text};
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
