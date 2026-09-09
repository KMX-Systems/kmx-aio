/// @file aio/knx/routing.hpp
/// @brief Routing/multicast capability and indication boundary.
/// @details
/// KNXnet/IP routing is the connectionless half of the protocol: routers put cEMI frames on a multicast
/// group and every listener sees them. There is no channel, no sequence number and no acknowledgement, so
/// nothing here resembles the tunnelling session - a ROUTING_INDICATION is a KNXnet/IP header followed
/// immediately by the frame.
///
/// What replaces the acknowledgement is flow control by announcement. A router that is falling behind
/// sends ROUTING_BUSY and senders are expected to hold off for the time it names; one that has already
/// dropped frames sends ROUTING_LOST_MESSAGE so the loss is at least observable. Both are honoured by
/// @ref kmx::aio::knx::routing::client rather than merely reported: a busy message pauses its sends, and
/// both are counted in @ref kmx::aio::knx::routing::statistics.
/// @reference KNX System Specifications, 03/08/02 "Core", routing.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <algorithm>
        #include <array>
        #include <cstdint>
        #include <expected>
        #include <span>
        #include <system_error>
        #include <variant>
        #include <vector>
    #endif

    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/frame.hpp>
    #include <kmx/aio/knx/transport.hpp>

namespace kmx::aio::knx::routing
{
    /// @brief A monotonic millisecond clock, for the busy back-off; null selects the steady clock.
    using clock_now_function = std::uint32_t (*)() noexcept;
    /// @brief Service type of a ROUTING_INDICATION.
    inline constexpr std::uint16_t indication_service = 0x0530u;
    /// @brief Service type of a ROUTING_LOST_MESSAGE.
    inline constexpr std::uint16_t lost_message_service = 0x0531u;
    /// @brief Service type of a ROUTING_BUSY.
    inline constexpr std::uint16_t busy_service = 0x0532u;
    // There is deliberately no indication_header_size here. Routing is connectionless: unlike
    // TUNNELLING_REQUEST, a ROUTING_INDICATION carries no connection header - no channel id, no sequence
    // counter - and the cEMI frame begins immediately after the six-octet KNXnet/IP header. A constant
    // fixed at zero would only invite "+ indication_header_size" back into size arithmetic that no longer
    // has a term for it.
    /// @brief Structure length octet of the lost message information block, and its total size.
    inline constexpr std::uint8_t lost_message_structure_size = 0x04u;
    /// @brief Structure length octet of the routing busy information block, and its total size.
    inline constexpr std::uint8_t busy_structure_size = 0x06u;
    /// @brief Body size of a ROUTING_LOST_MESSAGE: structure length, device state, lost message count.
    inline constexpr std::size_t lost_message_body_size = lost_message_structure_size;
    /// @brief Body size of a ROUTING_BUSY: structure length, device state, wait time, control field.
    inline constexpr std::size_t busy_body_size = busy_structure_size;
    /// @brief The multicast group a routing client joins; the transport's own configuration type.
    using multicast_configuration = multicast_group_configuration;

    /// @brief Checks that a multicast configuration names a usable group.
    /// @param value The configuration to check.
    /// @return Nothing, or @ref kmx::aio::knx::error::invalid_configuration.
    /// @details Only what would fail silently is checked: a zero port, and an address outside 224.0.0.0/4.
    ///          Joining a unicast address does not fail at the socket in any useful way - it simply never
    ///          delivers - which is why it is caught here instead.
    [[nodiscard]] constexpr std::expected<void, error> validate(
        const multicast_configuration& value) noexcept
    {
        if (value.port == 0u)
            return std::unexpected(error::invalid_configuration);
        if ((value.group[0u] < 224u) || (value.group[0u] > 239u))
            return std::unexpected(error::invalid_configuration);
        return {};
    }

    /// @brief One ROUTING_INDICATION: a cEMI frame addressed to the multicast group.
    /// @warning The octets are a view. They must outlive every encode or send call that uses them.
    struct indication
    {
        /// @brief The cEMI frame to send, without any KNXnet/IP header.
        cspan_uint8_t cemi_bytes {};
        /// @brief The decoded frame, as @ref decode_indication_packet read it.
        /// @details A decoder cannot report a datagram well formed without parsing the cEMI in it, so the
        ///          frame it already built is carried out rather than dropped: a receiver that wants the
        ///          addresses or the APCI would otherwise decode the very same octets a second time.
        ///          Left default-constructed when a caller fills an indication in to send, where nothing
        ///          reads it - @ref encode_indication_packet writes @ref cemi_bytes alone.
        cemi_frame cemi {};
    };

    /// @brief A received ROUTING_INDICATION, owning its cEMI octets.
    struct received_indication
    {
        /// @brief The received cEMI frame, copied out of the receive buffer.
        /// @details Held inline rather than in an owning buffer. Routing carries every frame on the bus,
        ///          so this is the most frequently produced object in the library, and a heap allocation
        ///          here was one per received frame.
        cemi_bytes_storage cemi_bytes {};
        /// @brief The decoded frame, as @ref decode_indication_packet read it.
        cemi_frame cemi {};
    };

    /// @brief One ROUTING_LOST_MESSAGE, reporting frames a router had to drop.
    struct lost_message
    {
        /// @brief The router's device state octet.
        std::uint8_t device_state {};
        /// @brief How many frames were lost.
        std::uint16_t count {};
    };

    /// @brief One ROUTING_BUSY, asking senders to back off.
    struct busy
    {
        /// @brief The router's device state octet.
        std::uint8_t device_state {};
        /// @brief How long to wait before sending again.
        std::uint16_t wait_time_ms {};
        /// @brief The control field that selects which senders the wait applies to.
        std::uint16_t control_field {};
    };

    /// @brief Anything a routing listener can be handed: a frame, or a router's flow-control report.
    using event = std::variant<received_indication, lost_message, busy>;

    /// @brief A received indication, or the error explaining why none was obtained.
    using received_indication_result_t = std::expected<received_indication, std::error_code>;
    /// @brief Task yielding a received indication or the error that stopped the receive.
    using received_indication_task_t = task<received_indication_result_t>;

    /// @brief A routing event, or the error explaining why none was obtained.
    using event_result_t = std::expected<event, std::error_code>;
    /// @brief Task yielding a routing event or the error that stopped the receive.
    using event_task_t = task<event_result_t>;

    /// @brief What a routing client has observed since it was created.
    /// @details Routing loses frames by design, so these counters are the only evidence an application has
    ///          that its multicast leg is healthy. They are never reset, including across a stop and start.
    struct statistics
    {
        /// @brief How many ROUTING_BUSY messages have been received.
        std::uint64_t busy_messages {};
        /// @brief How many frames routers have reported losing, summed over their ROUTING_LOST_MESSAGEs.
        std::uint64_t lost_messages {};
        /// @brief How many of this client's own datagrams came back to it off the multicast group.
        std::uint64_t reflected_messages {};
        /// @brief The wait time the most recent ROUTING_BUSY asked for.
        std::uint32_t busy_backoff_ms {};
    };

    /// @brief Encodes a ROUTING_INDICATION, header included.
    /// @param destination The destination octets; must be large enough for the encoded frame.
    /// @param value The cEMI frame to carry.
    /// @return Nothing, or the reason the frame could not be encoded.
    [[nodiscard]] expected_void_t encode_indication_packet(span_uint8_t destination, const indication& value) noexcept;

    /// @brief Decodes a ROUTING_INDICATION.
    /// @param packet The received octets, header included.
    /// @return The indication, or the reason the octets could not be read.
    /// @warning The returned @ref indication views @p packet; it does not own the cEMI octets.
    [[nodiscard]] std::expected<indication, std::error_code> decode_indication_packet(
        cspan_uint8_t packet) noexcept;

    /// @brief Encodes a ROUTING_LOST_MESSAGE, header included.
    /// @param destination The destination octets.
    /// @param value The device state and lost frame count to report.
    /// @return Nothing, or the reason the frame could not be encoded.
    [[nodiscard]] expected_void_t encode_lost_message_packet(span_uint8_t destination, const lost_message& value) noexcept;

    /// @brief Decodes a ROUTING_LOST_MESSAGE.
    /// @param packet The received octets, header included.
    /// @return The report, or the reason the octets could not be read.
    [[nodiscard]] std::expected<lost_message, std::error_code> decode_lost_message_packet(
        cspan_uint8_t packet) noexcept;

    /// @brief Encodes a ROUTING_BUSY, header included.
    /// @param destination The destination octets.
    /// @param value The device state, wait time and control field to report.
    /// @return Nothing, or the reason the frame could not be encoded.
    [[nodiscard]] expected_void_t encode_busy_packet(span_uint8_t destination, const busy& value) noexcept;

    /// @brief Decodes a ROUTING_BUSY.
    /// @param packet The received octets, header included.
    /// @return The report, or the reason the octets could not be read.
    [[nodiscard]] std::expected<busy, std::error_code> decode_busy_packet(
        cspan_uint8_t packet) noexcept;

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
        [[nodiscard]] virtual task_returning_expected_void_t send_indication(
            const indication& value) noexcept(false) = 0;
    };

    /// @brief A KNXnet/IP routing endpoint: joins the multicast group, sends and receives on it.
    /// @details Connectionless, so there is no session to keep - @ref start joins the group and @ref stop
    ///          leaves it, and everything between is individual datagrams. Two behaviours are worth knowing
    ///          about because they are not in the frames themselves: a received ROUTING_BUSY suspends
    ///          sending for the time it asks for, and a datagram identical to the last one sent is treated
    ///          as this client's own traffic reflected back by the group and dropped rather than delivered.
    class client final: public sender
    {
    public:
        /// @brief Creates a routing client.
        /// @param transport The executor-bound UDP transport to drive.
        /// @param configuration The multicast group to join; validated at @ref start.
        /// @param clock_now The monotonic millisecond clock; the steady clock when null.
        client(datagram_transport& transport,
                    multicast_configuration configuration = {},
                    clock_now_function clock_now = nullptr) noexcept:
                transport_(transport), configuration_(configuration), clock_now_(clock_now) {}

        /// @brief Validates the configuration and joins the multicast group.
        /// @return Nothing, or the reason the group could not be joined.
        /// @note Idempotent: starting an already-started client succeeds and does nothing.
        [[nodiscard]] expected_void_t start() noexcept;

        /// @brief Leaves the multicast group and clears the reflection and back-off state.
        /// @return Nothing, or the reason the group could not be left.
        /// @note Idempotent, and the counters survive; only the transient state is cleared.
        [[nodiscard]] expected_void_t stop() noexcept;

        /// @brief Returns what this client has observed since it was created.
        [[nodiscard]] const statistics& counters() const noexcept { return counters_; }

        /// @brief Records a router's back-off request and suspends sending for it.
        /// @param backoff_ms How long to hold off, from now.
        /// @note Called for every ROUTING_BUSY received. It is public so an application that learns of
        ///       congestion by other means can apply the same back-off.
        void note_busy(const std::uint32_t backoff_ms) noexcept;

        /// @brief Records one lost frame.
        void note_lost() noexcept;

        /// @brief Records one datagram of this client's own that came back off the group.
        void note_reflected() noexcept;

        /// @brief Puts one cEMI frame on the multicast group.
        /// @param value The frame to send; its octets must outlive the awaited task.
        /// @return A task yielding nothing, or the error that stopped the send.
        /// @retval kmx::aio::knx::error::invalid_configuration The client has not been started.
        /// @retval kmx::aio::knx::error::timeout A ROUTING_BUSY back-off is still in effect.
        [[nodiscard]] task_returning_expected_void_t send_indication(
            const indication& value) noexcept(false) override;

        /// @brief Sends a ROUTING_BUSY, asking other senders to hold off.
        /// @param value The device state, wait time and control field to report.
        /// @return A task yielding nothing, or the error that stopped the send.
        [[nodiscard]] task_returning_expected_void_t send_busy(
            const busy& value) noexcept(false);

        /// @brief Sends a ROUTING_LOST_MESSAGE, reporting frames this device had to drop.
        /// @param value The device state and lost frame count to report.
        /// @return A task yielding nothing, or the error that stopped the send.
        [[nodiscard]] task_returning_expected_void_t send_lost_message(
            const lost_message& value) noexcept(false);

        /// @brief Waits for the next routing event of any kind.
        /// @return A task yielding the event, or the error that stopped the receive.
        /// @retval kmx::aio::knx::error::invalid_configuration The client has not been started.
        /// @retval kmx::aio::knx::error::connection_failed The datagram came from an address the transport
        ///         could not attribute to a peer.
        /// @retval kmx::aio::knx::error::unsupported_service The datagram is a KNXnet/IP frame of a service
        ///         routing does not define.
        /// @note A datagram identical to one of the recent sends is counted as reflected and waited past, so this
        ///       never yields a client's own traffic back to it.
        [[nodiscard]] event_task_t receive_event() noexcept(false);

        /// @brief Waits for the next ROUTING_INDICATION, handling flow-control events on the way.
        /// @return A task yielding the received frame, or the error that stopped the receive.
        /// @note Busy and lost-message reports are still counted and acted on; they are simply not yielded.
        ///       Use @ref receive_event to see them.
        [[nodiscard]] received_indication_task_t receive_indication() noexcept(false);

    private:
        /// @brief Reports whether a datagram is this client's own multicast coming back to it.
        [[nodiscard]] bool is_own_reflection(cspan_uint8_t packet) const noexcept;
        void record_sent_packet(cspan_uint8_t packet) noexcept;
        /// @brief Turns one received datagram into the event it represents.
        [[nodiscard]] event_result_t to_event(std::uint16_t service, cspan_uint8_t packet) noexcept;
        /// @brief Turns a ROUTING_INDICATION into an owning event.
        [[nodiscard]] event_result_t to_indication(cspan_uint8_t packet) noexcept;
        [[nodiscard]] expected_socket_address_t multicast_peer() const noexcept;
        [[nodiscard]] static bool valid_source_peer(const transport_peer& peer) noexcept;
        [[nodiscard]] std::uint32_t now_ms() const noexcept;

        datagram_transport& transport_;
        multicast_configuration configuration_ {};
        bool started_ {};
        statistics counters_ {};
        clock_now_function clock_now_ {};
        std::uint32_t busy_until_ms_ {};
        inline static constexpr std::size_t reflection_history_size = 4u;
        struct sent_packet
        {
            std::array<std::uint8_t, frame::max_datagram_size> bytes {};
            std::size_t size {};
        };
        std::array<sent_packet, reflection_history_size> recent_sent_packets_ {};
        std::size_t recent_sent_packet_count_ {};
        std::size_t next_sent_packet_index_ {};
        std::array<std::uint8_t, frame::max_datagram_size> receive_buffer_ {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
