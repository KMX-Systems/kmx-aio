#include <kmx/aio/http3/frame.hpp>

namespace kmx::aio::http3
{
    const char* http3_error_category::name() const noexcept
    {
        return "kmx.aio.http3";
    }

    /// @brief Names the errors that end or refuse a whole connection.
    /// @param value The code to name.
    /// @return Its text, or nothing when it belongs to another group.
    [[nodiscard]] static constexpr std::string_view connection_message(const error_code value) noexcept
    {
        switch (value)
        {
            case error_code::no_error:
                return "no error";
            case error_code::general_protocol_error:
                return "general protocol error";
            case error_code::internal_error:
                return "internal error";
            case error_code::stream_creation_error:
                return "stream creation error";
            case error_code::closed_critical_stream:
                return "closed critical stream";
            case error_code::excessive_load:
                return "excessive load";
            case error_code::settings_error:
                return "settings error";
            case error_code::missing_settings:
                return "missing settings";
            case error_code::version_fallback:
                return "version fallback";
            default:
                return {};
        }
    }

    /// @brief Names the errors about one request, its frames or its message.
    /// @param value The code to name.
    /// @return Its text, or nothing when it belongs to another group.
    [[nodiscard]] static constexpr std::string_view request_message(const error_code value) noexcept
    {
        switch (value)
        {
            case error_code::frame_unexpected:
                return "frame unexpected";
            case error_code::frame_error:
                return "frame error";
            case error_code::id_error:
                return "id error";
            case error_code::request_rejected:
                return "request rejected";
            case error_code::request_cancelled:
                return "request cancelled";
            case error_code::request_incomplete:
                return "request incomplete";
            case error_code::message_error:
                return "message error";
            case error_code::connect_error:
                return "connect error";
            default:
                return {};
        }
    }

    std::string http3_error_category::message(const int ev) const
    {
        const auto value = static_cast<error_code>(static_cast<std::uint64_t>(ev));
        if (const auto text = connection_message(value); !text.empty())
            return std::string {text};
        if (const auto text = request_message(value); !text.empty())
            return std::string {text};
        return "unknown http3 error";
    }

    const std::error_category& http3_error_category_instance() noexcept
    {
        static http3_error_category category {};
        return category;
    }

    std::error_code make_error_code(const error_code code) noexcept
    {
        return {static_cast<int>(code), http3_error_category_instance()};
    }
} // namespace kmx::aio::http3
