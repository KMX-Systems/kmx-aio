/// @file api/kmx/aio/knx/routing.hpp
/// @brief KNXnet/IP routing indications, flow-control reports and their codecs.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
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
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/cemi_bytes_storage.hpp>
        #include <kmx/aio/knx/cemi_frame.hpp>
        #include <kmx/aio/knx/error.hpp>
        #include <kmx/aio/knx/secure/credentials.hpp>
        #include <kmx/aio/knx/transport.hpp>
        #include <kmx/aio/task.hpp>

        #include <cstddef>
        #include <cstdint>
        #include <expected>
        #include <system_error>
        #include <variant>
    #endif

namespace kmx::aio::knx::routing
{
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

    /// @brief Checks that a multicast configuration names a usable group.
    /// @param value The configuration to check.
    /// @return Nothing, or @ref kmx::aio::knx::error::invalid_configuration.
    /// @details Only what would fail silently is checked: a zero port, and an address outside 224.0.0.0/4.
    ///          Joining a unicast address does not fail at the socket in any useful way - it simply never
    ///          delivers - which is why it is caught here instead.
    [[nodiscard]] constexpr std::expected<void, error> validate(const multicast_configuration& value) noexcept
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
    [[nodiscard]] std::expected<indication, std::error_code> decode_indication_packet(cspan_uint8_t packet) noexcept;

    /// @brief Encodes a ROUTING_LOST_MESSAGE, header included.
    /// @param destination The destination octets.
    /// @param value The device state and lost frame count to report.
    /// @return Nothing, or the reason the frame could not be encoded.
    [[nodiscard]] expected_void_t encode_lost_message_packet(span_uint8_t destination, const lost_message& value) noexcept;

    /// @brief Decodes a ROUTING_LOST_MESSAGE.
    /// @param packet The received octets, header included.
    /// @return The report, or the reason the octets could not be read.
    [[nodiscard]] std::expected<lost_message, std::error_code> decode_lost_message_packet(cspan_uint8_t packet) noexcept;

    /// @brief Encodes a ROUTING_BUSY, header included.
    /// @param destination The destination octets.
    /// @param value The device state, wait time and control field to report.
    /// @return Nothing, or the reason the frame could not be encoded.
    [[nodiscard]] expected_void_t encode_busy_packet(span_uint8_t destination, const busy& value) noexcept;

    /// @brief Decodes a ROUTING_BUSY.
    /// @param packet The received octets, header included.
    /// @return The report, or the reason the octets could not be read.
    [[nodiscard]] std::expected<busy, std::error_code> decode_busy_packet(cspan_uint8_t packet) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
