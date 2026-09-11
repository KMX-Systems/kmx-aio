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
        #include <atomic>
        #include <cstdint>
        #include <expected>
        #include <memory>
        #include <optional>
        #include <span>
        #include <system_error>
        #include <variant>
        #include <vector>
    #endif

    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/frame.hpp>
    #include <kmx/aio/knx/secure/common.hpp>
    #include <kmx/aio/knx/secure/credentials.hpp>
    #include <kmx/aio/knx/secure/entropy.hpp>
    #include <kmx/aio/knx/transport.hpp>

namespace kmx::aio::knx::data_secure
{
    class context;
}

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
    /// @brief What a KNX IP Secure routing client takes: backbone key, latency tolerance, serial number and duplicate
    ///        cache size, usually built by @ref kmx::aio::knx::keyring::routing_configuration_for.
    using secure_configuration = secure::routing_configuration;

    namespace detail
    {
        /// @brief The state a secure routing client holds beyond a plain one; defined with the client.
        struct secure_routing;
    }

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
    ///
    ///          Constructed with a @ref secure_configuration, the client speaks KNX IP Secure routing. Every frame
    ///          it sends goes out in a SECURE_WRAPPER under the backbone key, stamped with its routing timer, and
    ///          routing traffic that arrives unwrapped is refused and counted rather than delivered (P1). A wrapper
    ///          is delivered only once its MAC verifies, its timer is inside the latency window, and it is not a
    ///          repeat; nothing about the timer changes before the MAC verifies (P2). Discovery and other services
    ///          routing does not carry are left to the caller exactly as without security. Nothing runs a timer:
    ///          @ref next_timer_deadline_ms says when @ref notify_timer has a TIMER_NOTIFY to send, and a supervisor
    ///          awaits it then. Receives, sends and @ref notify_timer may run concurrently on different threads;
    ///          what they share is guarded by an @ref kmx::aio::async_mutex that is never held across I/O.
    class client final: public sender
    {
    public:
        /// @brief Creates a routing client.
        /// @param transport The executor-bound UDP transport to drive.
        /// @param configuration The multicast group to join; validated at @ref start.
        /// @param clock_now The monotonic millisecond clock; the steady clock when null.
        client(datagram_transport& transport, multicast_configuration configuration = {}, clock_now_function clock_now = nullptr) noexcept;

        /// @brief Creates a KNX IP Secure routing client.
        /// @param transport The executor-bound UDP transport to drive.
        /// @param configuration The multicast group to join; use the keyring's multicast address.
        /// @param secure_settings The backbone key, latency tolerance and serial number; checked at @ref start.
        /// @param clock_ms The 64-bit monotonic millisecond clock the routing timer runs on; the steady clock when null.
        /// @param entropy Where message tags and notify delays come from; @ref kmx::aio::knx::secure::system_entropy
        ///        when null, and otherwise it must outlive the client.
        /// @param clock_now The 32-bit clock the busy back-off uses; the steady clock when null.
        /// @throws std::bad_alloc when the secure state cannot be allocated.
        client(datagram_transport& transport, multicast_configuration configuration, secure_configuration secure_settings,
               secure::monotonic_ms_function clock_ms = nullptr, secure::entropy_source* entropy = nullptr,
               clock_now_function clock_now = nullptr) noexcept(false);

        /// @brief Destroys the client, wiping the backbone key it holds.
        ~client() noexcept override;

        /// @brief Validates the configuration and joins the multicast group.
        /// @return Nothing, or the reason the group could not be joined.
        /// @retval kmx::aio::knx::error::invalid_configuration The group is unusable, or a secure client has an
        ///         all-zero serial number (P8).
        /// @retval kmx::aio::knx::error::secure_key_missing A secure client has no backbone key.
        /// @note Idempotent: starting an already-started client succeeds and does nothing. A secure client that
        ///       starts owes a synchronisation request, which @ref notify_timer sends.
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

        /// @brief Indicates whether this client speaks KNX IP Secure routing.
        [[nodiscard]] bool secured() const noexcept { return secure_ != nullptr; }

        /// @brief Applies KNX Data Secure to the group telegrams this client routes, or stops applying it.
        /// @param context The Data Secure context, which must outlive its use here; null to stop.
        /// @details A frame @ref send_indication puts on the group is secured when it is a group telegram to a group with a
        ///          key. Every indication @ref receive_event and @ref receive_indication yield is opened first, and a
        ///          telegram the context refuses is counted there and read past. A router that only forwards Data Secure
        ///          APDUs, as a gateway does, sets no context.
        void use_data_secure(data_secure::context* const context) noexcept { data_secure_.store(context); }

        /// @brief When @ref notify_timer next has a TIMER_NOTIFY to send, on the secure clock.
        /// @return Zero while a synchronisation request is owed; the maximum value for a client that is not secure.
        /// @note Safe to call from any thread at any time.
        [[nodiscard]] std::uint64_t next_timer_deadline_ms() const noexcept;

        /// @brief Indicates whether the secure routing timer has synchronised, by an answer or by a timeout, so that
        ///        received wrappers are being accepted; false for a client that is not secure.
        /// @note Safe to call from any thread at any time. A telegram sent before this turns true may draw a reply
        ///       that arrives while wrappers are still being dropped.
        [[nodiscard]] bool timer_synchronised() const noexcept;

        /// @brief Sends the TIMER_NOTIFY that is due, if one is; send-only, so it may run while a receive waits.
        /// @return A task yielding nothing, or the error that stopped the send.
        /// @retval kmx::aio::knx::error::invalid_configuration The client is not started or not secure.
        /// @details The first call after @ref start sends the synchronisation request. Later calls send a periodic
        ///          or an update notify when @ref next_timer_deadline_ms has passed, and do nothing otherwise.
        [[nodiscard]] task_returning_expected_void_t notify_timer() noexcept(false);

        /// @brief Returns what the secure layer has refused and done; all zero for a client that is not secure.
        /// @note Read it when no receive, send or notify is in flight.
        [[nodiscard]] const secure::statistics& secure_counters() const noexcept;

    private:
        /// @brief A received datagram's event, or nothing when it was consumed without producing one.
        using optional_event_t = std::optional<event_result_t>;
        /// @brief Whether a TIMER_NOTIFY was prepared for sending, or why none could be.
        using prepared_notify_t = std::expected<bool, std::error_code>;

        /// @brief Reports whether a datagram is this client's own multicast coming back to it.
        [[nodiscard]] bool is_own_reflection(cspan_uint8_t packet) const noexcept;
        void record_sent_packet(cspan_uint8_t packet) noexcept;
        /// @brief Turns one received datagram into the event it represents.
        [[nodiscard]] event_result_t to_event(std::uint16_t service, cspan_uint8_t packet) noexcept;
        /// @brief Turns a ROUTING_INDICATION into an owning event.
        [[nodiscard]] event_result_t to_indication(cspan_uint8_t packet) noexcept;
        /// @brief Opens a received indication with Data Secure when a context is set; false when the context refused it.
        [[nodiscard]] bool open_data_secure(event_result_t& outcome) const noexcept(false);
        [[nodiscard]] expected_socket_address_t multicast_peer() const noexcept;
        [[nodiscard]] static bool valid_source_peer(const transport_peer& peer) noexcept;
        [[nodiscard]] std::uint32_t now_ms() const noexcept;

        /// @brief Refuses a secure configuration that cannot run: no serial number (P8), or no key.
        [[nodiscard]] expected_void_t validate_secure() const noexcept;
        /// @brief Sends one encoded datagram to the group, wrapping it first when the client is secure.
        [[nodiscard]] task_returning_expected_void_t send_packet(cspan_uint8_t packet) noexcept(false);
        /// @brief Wraps an outgoing datagram under the timer, recording it as sent before it leaves.
        [[nodiscard]] task_returning_expected_size_t seal_outgoing(span_uint8_t destination, cspan_uint8_t packet) noexcept(false);
        /// @brief Builds the TIMER_NOTIFY that is due into @p packet; false when none is.
        [[nodiscard]] task<prepared_notify_t> prepare_timer_notify(span_uint8_t packet) noexcept(false);
        /// @brief Handles one datagram without security.
        [[nodiscard]] optional_event_t plain_event(cspan_uint8_t packet) noexcept;
        /// @brief Handles one datagram with security, under the mutex.
        [[nodiscard]] task<optional_event_t> secure_event(cspan_uint8_t packet) noexcept(false);
        /// @brief Dispatches a datagram that is not a reflection; the mutex is held.
        [[nodiscard]] optional_event_t secured_event(std::uint16_t service, cspan_uint8_t packet) noexcept;
        /// @brief Verifies and unwraps a SECURE_WRAPPER; the mutex is held.
        [[nodiscard]] optional_event_t unwrap_event(cspan_uint8_t packet) noexcept;
        /// @brief Verifies and applies a TIMER_NOTIFY; the mutex is held.
        void apply_timer_notify(cspan_uint8_t packet) noexcept;

        datagram_transport& transport_;
        multicast_configuration configuration_ {};
        bool started_ {};
        statistics counters_ {};
        clock_now_function clock_now_ {};
        std::atomic<std::uint32_t> busy_until_ms_ {};
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
        std::unique_ptr<detail::secure_routing> secure_ {};
        /// @brief The Data Secure context applied to group telegrams, when one is set; see @ref use_data_secure.
        std::atomic<data_secure::context*> data_secure_ {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
