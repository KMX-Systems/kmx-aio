/// @file aio/http3/control.hpp
/// @brief HTTP/3 control stream definitions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <kmx/aio/http3/frame.hpp>
#include <kmx/aio/http3/settings.hpp>

#include <optional>

namespace kmx::aio::http3
{
    /// @brief Minimal decoded view of the HTTP/3 control stream state.
    struct control_stream_state
    {
        /// @brief True when the mandatory SETTINGS frame has been parsed.
        bool saw_settings {false};
        /// @brief The decoded SETTINGS values negotiated on the control stream.
        settings negotiated_settings {};
        /// @brief Optional GOAWAY frame observed on the control stream.
        std::optional<goaway_frame> goaway {};
    };
} // namespace kmx::aio::http3