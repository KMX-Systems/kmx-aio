/// @file api/kmx/aio/knx/server.hpp
/// @brief The frames a KNXnet/IP tunnelling server hands to the application, and the handler that takes them.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/task.hpp>

        #include <cstdint>
        #include <expected>
        #include <functional>
        #include <system_error>
    #endif

namespace kmx::aio::knx
{
    /// @brief One cEMI frame a client tunnelled in, and the channel it arrived on.
    /// @note The channel matters as much as the frame: it is what an answer is sent back on, and what says
    ///       which client's assigned address the frame was sent under.
    struct server_event
    {
        /// @brief The channel the frame arrived on.
        std::uint8_t channel_id {};
        /// @brief The cEMI frame, copied out of the receive buffer.
        byte_buffer_t cemi_bytes {};
    };

    /// @brief A server event, or the error explaining why none was obtained.
    using server_event_result_t = std::expected<server_event, std::error_code>;
    /// @brief Task yielding a server event or the error that stopped the exchange.
    using server_event_task_t = task<server_event_result_t>;
    /// @brief Takes the frames a connection tunnels in, one at a time, as
    ///        @ref kmx::aio::knx::generic_server::serve_connection receives them.
    /// @note Awaited before the connection's next frame is read, so a slow handler holds back its own connection only.
    using server_event_handler = std::function<task<void>(server_event)>;
}
#endif // KMX_AIO_FEATURE_KNX
