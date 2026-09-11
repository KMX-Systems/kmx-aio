/// @file inc/kmx/aio/avb/srp/primary_client.hpp
/// @brief Executor-agnostic implementation state of the IEEE 802.1Qat SRP (MSRP) client.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/avb/avb_types.hpp>
    #include <kmx/aio/avb/srp/messages.hpp>

    #include <cstddef>
    #include <map>
    #include <optional>
    #include <vector>
#endif

namespace kmx::aio::avb::srp
{
    /// @brief Executor-agnostic part of the SRP client: declarations, PDU encoding and decoding.
    /// @details Holds everything that does not depend on the executor type, so the code is emitted once
    ///          instead of once per pillar. `generic_client<Executor>::state` adds the socket and the
    ///          executor-specific coroutine loops.
    struct primary_client
    {
        /// @brief Strict-weak ordering over `stream_id_t`, so it can key the declaration maps.
        struct stream_id_less
        {
            /// @brief Orders by source MAC first, then by unique id.
            /// @param lhs Left operand.
            /// @param rhs Right operand.
            /// @return `true` when @p lhs sorts before @p rhs.
            [[nodiscard]] bool operator()(const stream_id_t& lhs, const stream_id_t& rhs) const noexcept
            {
                if (lhs.source_mac != rhs.source_mac)
                    return lhs.source_mac < rhs.source_mac;
                return lhs.unique_id < rhs.unique_id;
            }
        };

        /// @brief One in-flight `subscribe` call awaiting a matching Talker Advertise.
        struct sub_waiter
        {
            /// @brief The stream being waited on.
            stream_id_t id {};
            /// @brief The descriptor decoded from the advertise, once one arrives.
            std::optional<stream_descriptor> resolved {};
        };

        // Talker: streams we are advertising (stream_id → descriptor)

        /// @brief Streams this node advertises as a talker.
        std::map<stream_id_t, stream_descriptor, stream_id_less> talker_streams_ {};

        // Listener: streams we have subscribed to

        /// @brief Streams this node has subscribed to as a listener.
        std::map<stream_id_t, stream_descriptor, stream_id_less> listener_streams_ {};

        // Pending subscribe waiters: stream_id → resolved descriptor.
        // Stored in a map so coroutine references remain stable across suspension.

        /// @brief Waiters keyed by stream id; map nodes keep references stable across suspension.
        std::map<stream_id_t, sub_waiter, stream_id_less> pending_subs_ {};

        // Encode helpers

        /// @brief Encodes one MSRP Talker Advertise PDU for a stream.
        /// @param desc The stream being advertised.
        /// @return The frame bytes ready to hand to the socket.
        [[nodiscard]] static std::vector<std::byte> build_talker_advertise(const stream_descriptor& desc) noexcept;

        /// @brief Encodes one MSRP Listener Ready PDU for a stream.
        /// @param desc The stream being subscribed to.
        /// @return The frame bytes ready to hand to the socket.
        [[nodiscard]] static std::vector<std::byte> build_listener_ready(const stream_descriptor& desc) noexcept;

        /// @brief Encodes the SR Class A domain declaration PDU.
        /// @return The frame bytes ready to hand to the socket.
        [[nodiscard]] static std::vector<std::byte> build_domain() noexcept;

        // Frame dispatch

        /// @brief Routes one received MSRP frame to the matching handler.
        /// @param data Frame bytes; frames shorter than a message header are dropped.
        /// @param len  Frame length.
        void dispatch(const std::byte* data, std::size_t len) noexcept;

        /// @brief Decodes a Talker Advertise and resolves any `subscribe` waiter for that stream.
        /// @param data Frame bytes.
        /// @param len  Frame length.
        void on_talker_advertise(const std::byte* data, std::size_t len) noexcept;
    };
}
