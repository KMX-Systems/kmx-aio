/// @file src/kmx/aio/http3/control_stream_codec.cpp
/// @brief HTTP/3 control stream encoding and decoding: the opening SETTINGS, GOAWAY and frame rules.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/http3/control_stream_codec.hpp>
#ifndef PCH
    #include <kmx/aio/http3/control.hpp>
    #include <kmx/aio/http3/detail/varint.hpp>
    #include <kmx/aio/http3/frame.hpp>
    #include <kmx/aio/http3/frame_codec.hpp>
    #include <kmx/aio/http3/goaway_codec.hpp>
    #include <kmx/aio/http3/settings_codec.hpp>
#endif

namespace kmx::aio::http3
{
    std::vector<std::uint8_t> control_stream_codec::encode_opening(const settings& value) noexcept(false)
    {
        std::vector<std::uint8_t> bytes;
        detail::encode_varint(bytes, static_cast<std::uint64_t>(stream_type::control));
        const auto settings_frame = settings_codec::encode_frame(value);
        bytes.insert(bytes.end(), settings_frame.begin(), settings_frame.end());
        return bytes;
    }

    std::vector<std::uint8_t> control_stream_codec::append_goaway(cspan_uint8_t control_stream_bytes,
                                                                  const goaway_frame& value) noexcept(false)
    {
        std::vector<std::uint8_t> bytes(control_stream_bytes.begin(), control_stream_bytes.end());
        const auto goaway = goaway_codec::encode_frame(value);
        bytes.insert(bytes.end(), goaway.begin(), goaway.end());
        return bytes;
    }

    /// @brief Folds one control stream frame into the state being built.
    /// @param state The state to update.
    /// @param frame The frame to apply.
    /// @return Nothing, or why the frame does not belong on a control stream.
    [[nodiscard]] static std::expected<void, std::error_code> apply_control_frame(control_stream_state& state, const frame& frame) noexcept
    {
        switch (frame.type)
        {
            case frame_type::settings:
            {
                // Settings are sent once. A second SETTINGS frame is a protocol error, not an update.
                if (state.saw_settings)
                    return std::unexpected(make_error_code(error_code::settings_error));

                auto settings = settings_codec::decode(frame.payload);
                if (!settings)
                    return std::unexpected(settings.error());

                state.saw_settings = true;
                state.negotiated_settings = *settings;
                return {};
            }
            case frame_type::goaway:
            {
                auto goaway = goaway_codec::decode(frame.payload);
                if (!goaway)
                    return std::unexpected(goaway.error());

                state.goaway = *goaway;
                return {};
            }
            case frame_type::data:
            case frame_type::headers:
            case frame_type::push_promise:
                // Request frames on a control stream: the peer has confused its streams.
                return std::unexpected(make_error_code(error_code::frame_unexpected));
            default:
                // CANCEL_PUSH and MAX_PUSH_ID are accepted but not yet modelled, as is anything newer.
                return {};
        }
    }

    std::expected<control_stream_state, std::error_code> control_stream_codec::decode(cspan_uint8_t payload) noexcept
    {
        auto stream_type_value = detail::decode_varint(payload, 0u);
        if (!stream_type_value)
            return std::unexpected(stream_type_value.error());
        if (static_cast<stream_type>(stream_type_value->first) != stream_type::control)
            return std::unexpected(make_error_code(error_code::stream_creation_error));

        control_stream_state state {};
        const auto frames = frame_codec::decode_all(payload.subspan(stream_type_value->second));
        if (!frames)
            return std::unexpected(frames.error());

        bool first_frame = true;

        for (const auto& frame: *frames)
        {
            if (first_frame)
            {
                first_frame = false;
                if (frame.type != frame_type::settings)
                    return std::unexpected(make_error_code(error_code::missing_settings));
            }

            if (const auto applied = apply_control_frame(state, frame); !applied.has_value())
                return std::unexpected(applied.error());
        }

        if (!state.saw_settings)
            return std::unexpected(make_error_code(error_code::missing_settings));

        return state;
    }
}
