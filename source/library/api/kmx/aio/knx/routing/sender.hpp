/// @file api/kmx/aio/knx/routing/sender.hpp
/// @brief The narrowest thing that can put a KNXnet/IP routing frame on the multicast group.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/knx/routing.hpp>
        #include <kmx/aio/task.hpp>
    #endif

namespace kmx::aio::knx::routing
{
    /// @brief The narrowest thing that can put a frame on the multicast group.
    /// @details Separated from @ref client so code that only publishes indications - a gateway's forwarding
    ///          path, a test double - depends on the one operation it uses rather than on the whole client.
    class sender
    {
    public:
        /// @brief Constructs a sender.
        sender() noexcept = default;
        sender(const sender&) = delete;
        sender& operator=(const sender&) = delete;
        /// @brief Destroys the sender.
        virtual ~sender() noexcept = default;

        /// @brief Puts one cEMI frame on the multicast group.
        /// @param value The frame to send; its octets must outlive the awaited task.
        /// @return A task yielding nothing, or the error that stopped the send.
        [[nodiscard]] virtual task_returning_expected_void_t send_indication(const indication& value) noexcept(false) = 0;
    };
}
#endif // KMX_AIO_FEATURE_KNX
