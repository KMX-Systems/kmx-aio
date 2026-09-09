/// @file aio/knx/frame.hpp
/// @brief Primitive KNXnet/IP frame and cEMI decode helpers.
/// @details
/// The bottom of the KNXnet/IP stack: the six-octet header every datagram begins with, the connection
/// header the connection-oriented services add, and the services that carry a cEMI message across an
/// established channel - tunnelling, device configuration, and the tunnelling features.
///
/// The frames here hold their cEMI octets inline rather than in a `std::vector`, because these types are
/// decoded on the receive path of every telegram and an allocation per frame is a cost the protocol does
/// not require. The inline capacities are hard bounds, not assumptions: an oversized message is reported
/// as @ref kmx::aio::knx::error::invalid_length rather than truncated.
/// @reference KNX System Specifications, 03/08/02 "Core" and 03/08/04 "Tunnelling".
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
        #include <vector>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/cemi.hpp>
    #include <kmx/aio/knx/contract.hpp>
    #include <kmx/aio/knx/error.hpp>

namespace kmx::aio::knx
{
    /// @brief The fixed six-octet header every KNXnet/IP datagram starts with.
    struct communication_header
    {
        /// @brief The protocol version; only `0x10` is defined.
        std::uint8_t protocol_version = 0x10u;
        /// @brief The service type identifier.
        std::uint16_t service_type {};
        /// @brief The datagram length including this header.
        std::uint16_t total_length {};
    };

    /// @brief Inline storage for the cEMI octets of one decoded tunnelling request.
    /// @details Sized to @ref kmx::aio::knx::cemi::max_message_size, the largest message the cEMI decoder
    ///          can accept, rather than to `max_l_data_size`, which describes only what this build's
    ///          encoder emits and is 255 octets short of what a peer may legitimately send.
    struct cemi_bytes_storage
    {
        /// @brief The storage; only its first @ref size octets are meaningful.
        std::array<std::uint8_t, cemi::max_message_size> bytes {};
        /// @brief How many octets of @ref bytes the decoder filled in.
        std::uint16_t size {};

        /// @brief Indicates whether no octets are held.
        [[nodiscard]] constexpr bool empty() const noexcept { return size == 0u; }
        /// @brief Returns how many octets are held.
        [[nodiscard]] constexpr std::size_t length() const noexcept { return size; }
        /// @brief Returns an iterator to the first octet.
        [[nodiscard]] constexpr auto begin() const noexcept { return bytes.begin(); }
        /// @brief Returns an iterator one past the last meaningful octet.
        [[nodiscard]] constexpr auto end() const noexcept { return bytes.begin() + size; }
        /// @brief Returns a view of the meaningful octets.
        [[nodiscard]] constexpr cspan_uint8_t span() const noexcept { return {bytes.data(), size}; }

        /// @brief Compares the held octets against an owning buffer.
        /// @param lhs The inline storage.
        /// @param rhs The buffer to compare against.
        /// @return `true` when both hold the same octets.
        /// @note Compares only the meaningful prefix, not the unused tail of @ref bytes.
        [[nodiscard]] friend bool operator==(const cemi_bytes_storage& lhs,
                                             const byte_buffer_t& rhs) noexcept
        {
            return (lhs.span().size() == rhs.size()) &&
                   std::equal(lhs.span().begin(), lhs.span().end(), rhs.begin());
        }
    };

    /// @brief One TUNNELLING_REQUEST: a connection header and the cEMI message it carries.
    /// @note Both the decoded message and its octets are kept. A @ref kmx::aio::knx::cemi_frame names its
    ///       payload by offset into the octets it was decoded from, so neither is usable without the other.
    struct tunnelling_request_frame
    {
        /// @brief The channel the request belongs to.
        std::uint8_t channel_id {};
        /// @brief The request's sequence number, which its acknowledgement must echo.
        std::uint8_t sequence_number {};
        /// @brief The length the KNXnet/IP header declared for the whole datagram.
        std::uint16_t message_length {};
        /// @brief The decoded cEMI message.
        cemi_frame cemi {};
        /// @brief The cEMI octets the message was decoded from.
        cemi_bytes_storage cemi_bytes {};
    };

    /// @brief One DEVICE_CONFIGURATION_REQUEST: a connection header and a cEMI management message.
    /// @details Unlike @ref kmx::aio::knx::tunnelling_request_frame this carries no decoded cEMI. A device
    ///          management payload is an M_Prop or M_Reset service rather than L_Data, so it is decoded by
    ///          @ref kmx::aio::knx::cemi::decode_property once the caller has looked at its message code.
    struct device_configuration_frame
    {
        /// @brief The management channel the request belongs to.
        std::uint8_t channel_id {};
        /// @brief The request's sequence number, which its acknowledgement must echo.
        std::uint8_t sequence_number {};
        /// @brief The length the KNXnet/IP header declared for the whole datagram.
        std::uint16_t message_length {};
        /// @brief The cEMI management message, undecoded.
        cemi_bytes_storage cemi_bytes {};
    };

    /// @brief The tunnelling features a KNXnet/IP server exposes on a tunnelling connection.
    /// @reference KNX System Specifications, 03/08/04 "Tunnelling", tunnelling feature identifiers.
    enum class tunnelling_feature : std::uint8_t
    {
        /// @brief Which EMI types the server supports.
        supported_emi_type = 0x01u,
        /// @brief The host device's descriptor type 0.
        host_device_descriptor = 0x02u,
        /// @brief Whether the server is connected to the bus.
        bus_connection_status = 0x03u,
        /// @brief The manufacturer code of the host device.
        manufacturer_code = 0x04u,
        /// @brief The EMI type currently in use.
        active_emi_type = 0x05u,
        /// @brief The individual address assigned to this tunnel.
        individual_address = 0x06u,
        /// @brief The largest APDU the connection can carry.
        max_apdu_length = 0x07u,
        /// @brief Whether the server sends TUNNELLING_FEATURE_INFO.
        info_service_enable = 0x08u,
    };

    /// @brief Inline storage for a tunnelling feature value.
    /// @details Every defined feature value is one or two octets; the capacity here is generous for them
    ///          and is a hard bound rather than an assumption, so a peer cannot make the decoder write past
    ///          it. A longer value is reported as `error::invalid_length` instead of being truncated.
    struct tunnelling_feature_value
    {
        /// @brief Largest feature value this build accepts.
        static constexpr std::size_t capacity = 16u;

        /// @brief The storage; only its first @ref size octets are meaningful.
        std::array<std::uint8_t, capacity> bytes {};
        /// @brief How many octets of @ref bytes the value occupies.
        std::uint8_t size {};

        /// @brief Indicates whether the value is absent, as it is in a get request.
        [[nodiscard]] constexpr bool empty() const noexcept { return size == 0u; }
        /// @brief Returns how many octets the value occupies.
        [[nodiscard]] constexpr std::size_t length() const noexcept { return size; }
        /// @brief Returns an iterator to the first octet.
        [[nodiscard]] constexpr auto begin() const noexcept { return bytes.begin(); }
        /// @brief Returns an iterator one past the last meaningful octet.
        [[nodiscard]] constexpr auto end() const noexcept { return bytes.begin() + size; }
        /// @brief Returns a view of the meaningful octets.
        [[nodiscard]] constexpr cspan_uint8_t span() const noexcept { return {bytes.data(), size}; }
    };

    /// @brief One of the four tunnelling feature services.
    /// @details All four share a shape: the connection header, the feature identifier, one octet that is the
    ///          return code in a response and reserved elsewhere, and a value that only some of them carry.
    struct tunnelling_feature_frame
    {
        /// @brief Which of the four services this is.
        std::uint16_t service_type {};
        /// @brief The channel the frame belongs to.
        std::uint8_t channel_id {};
        /// @brief The frame's sequence number.
        std::uint8_t sequence_number {};
        /// @brief Which feature is being read, written or reported.
        tunnelling_feature feature = tunnelling_feature::supported_emi_type;
        /// @brief The return code of a response; reserved and zero in the other three services.
        std::uint8_t return_code {};
        /// @brief The feature value; empty in a get request, which carries none.
        tunnelling_feature_value value {};
    };

    /// @brief One TUNNELLING_ACK: the answer every TUNNELLING_REQUEST expects.
    struct tunnelling_ack_frame
    {
        /// @brief The channel being acknowledged on.
        std::uint8_t channel_id {};
        /// @brief The sequence number of the request being acknowledged.
        std::uint8_t sequence_number {};
        /// @brief The outcome; zero means the request was accepted.
        std::uint8_t status {};
    };

    namespace frame
    {
        /// @brief Service type of a TUNNELLING_REQUEST.
        inline constexpr std::uint16_t tunnelling_request_service = 0x0420u;
        /// @brief Service type of a TUNNELLING_ACK.
        inline constexpr std::uint16_t tunnelling_ack_service = 0x0421u;
        /// @brief DEVICE_CONFIGURATION_REQUEST, which carries a cEMI device management message.
        /// @details Framed exactly like a tunnelling request - the same connection header, a different cEMI
        ///          payload - because it is the same connection machinery over a management channel.
        inline constexpr std::uint16_t device_configuration_request_service = 0x0310u;
        /// @brief DEVICE_CONFIGURATION_ACK.
        inline constexpr std::uint16_t device_configuration_ack_service = 0x0311u;
        /// @brief TUNNELLING_FEATURE_GET, which asks the server for one tunnelling feature.
        inline constexpr std::uint16_t tunnelling_feature_get_service = 0x0422u;
        /// @brief TUNNELLING_FEATURE_RESPONSE, the answer to a get or a set.
        inline constexpr std::uint16_t tunnelling_feature_response_service = 0x0423u;
        /// @brief TUNNELLING_FEATURE_SET, which writes one tunnelling feature.
        inline constexpr std::uint16_t tunnelling_feature_set_service = 0x0424u;
        /// @brief TUNNELLING_FEATURE_INFO, an unsolicited report that a feature changed.
        inline constexpr std::uint16_t tunnelling_feature_info_service = 0x0425u;
        /// @brief Largest datagram the KNXnet/IP total length field can describe.
        inline constexpr std::size_t max_frame_size = 0xFFFFu;
        /// @brief Largest datagram this build buffers, the operational limit behind the protocol maximum.
        /// @details One IPv4 UDP payload on an Ethernet link, which is an order of magnitude more than any
        ///          KNXnet/IP service needs: the longest tunnelling frame a cEMI message can fill is about
        ///          530 octets. Buffering the protocol maximum instead would put 64 KiB on every coroutine
        ///          frame that sends a telegram of a couple of dozen bytes.
        inline constexpr std::size_t max_datagram_size = 1472u;
        /// @brief Size of the KNXnet/IP header every datagram begins with.
        inline constexpr std::size_t communication_header_size = 6u;
        /// @brief Smallest cEMI L_Data message the decoder accepts.
        inline constexpr std::size_t cemi_min_size = cemi::min_l_data_size;
        /// @brief Largest cEMI message a decoded tunnelling request can carry, and the size of its storage.
        inline constexpr std::size_t cemi_max_size = cemi::max_message_size;
        /// @brief Size of the KNXnet/IP connection header both tunnelling services carry.
        /// @details Structure length, communication channel id, sequence counter, and one octet that is
        ///          reserved in a request and carries the status in an acknowledgement.
        /// @reference KNX System Specifications, 03/08/02 "Core", Connection Header.
        inline constexpr std::size_t tunnelling_request_header_size = 4u;
        /// @brief Body size of a TUNNELLING_ACK, which is the connection header and nothing more.
        inline constexpr std::size_t tunnelling_ack_size = 4u;
        /// @brief Value of the connection header's own structure length octet.
        /// @warning This octet is the first of the connection header, ahead of the channel id. Omitting it
        ///          and starting at the channel id yields a header of the right length whose every field is
        ///          one octet early - which no interface accepts, and which two ends of this library would
        ///          nonetheless exchange happily.
        inline constexpr std::uint8_t connection_header_structure_length = 0x04u;

        /// @brief Decodes the six-octet KNXnet/IP header.
        /// @param buf The received octets; only the header is read.
        /// @return The header, or the reason it could not be read.
        /// @note The declared total length is returned as it stands and is not checked against @p buf; the
        ///       service decoders do that, since only they know how long their own body must be.
        [[nodiscard]] std::expected<communication_header, std::error_code> decode_communication_header(cspan_uint8_t buf) noexcept;
        /// @brief Encodes the six-octet KNXnet/IP header.
        /// @param dest The destination octets.
        /// @param service_type The service the datagram carries.
        /// @param total_length The whole datagram's length, this header included.
        /// @param protocol_version The protocol version; only `0x10` is defined.
        /// @return Nothing, or the reason the header could not be encoded.
        [[nodiscard]] expected_void_t encode_communication_header(span_uint8_t dest,
                                                                                     std::uint16_t service_type,
                                                                                     std::uint16_t total_length,
                                                                                     std::uint8_t protocol_version = 0x10u) noexcept;
        /// @brief Decodes a bare cEMI message, with no KNXnet/IP framing around it.
        /// @param buf The cEMI octets.
        /// @return The decoded message, or the reason it could not be read.
        /// @warning The result names its payload by offset into @p buf, so @p buf must outlive it.
        [[nodiscard]] std::expected<cemi_frame, std::error_code> decode_cemi(cspan_uint8_t buf) noexcept;
        /// @brief Encodes a TUNNELLING_REQUEST body: connection header and cEMI, without the KNXnet/IP
        ///        header.
        /// @param dest The destination octets.
        /// @param channel_id The channel to send on.
        /// @param sequence_number The sequence number to send under.
        /// @param cemi_bytes The cEMI message to carry.
        /// @return Nothing, or the reason the body could not be encoded.
        /// @note @ref encode_tunnelling_request_packet is what produces a sendable datagram; this exists
        ///       for a caller assembling one itself.
        [[nodiscard]] expected_void_t encode_tunnelling_request(span_uint8_t dest,
                                                                                     std::uint8_t channel_id,
                                                                                     std::uint8_t sequence_number,
                                                                                     cspan_uint8_t cemi_bytes) noexcept;
        /// @brief Decodes a TUNNELLING_REQUEST body, without the KNXnet/IP header.
        /// @param buf The body octets, starting at the connection header.
        /// @return The decoded request, or the reason it could not be read.
        [[nodiscard]] std::expected<tunnelling_request_frame, std::error_code> decode_tunnelling_request(cspan_uint8_t buf) noexcept;
        /// @brief Decodes a TUNNELLING_ACK body, without the KNXnet/IP header.
        /// @param buf The body octets, starting at the connection header.
        /// @return The decoded acknowledgement, or the reason it could not be read.
        [[nodiscard]] std::expected<tunnelling_ack_frame, std::error_code> decode_tunnelling_ack(cspan_uint8_t buf) noexcept;
        /// @brief Encodes a complete TUNNELLING_REQUEST datagram.
        /// @param dest The destination octets; must be large enough for the encoded datagram.
        /// @param channel_id The channel to send on.
        /// @param sequence_number The sequence number to send under.
        /// @param cemi_bytes The cEMI message to carry.
        /// @return Nothing, or the reason the datagram could not be encoded.
        [[nodiscard]] expected_void_t encode_tunnelling_request_packet(
            span_uint8_t dest, std::uint8_t channel_id, std::uint8_t sequence_number, cspan_uint8_t cemi_bytes) noexcept;
        /// @brief Encodes a complete TUNNELLING_ACK datagram.
        /// @param dest The destination octets.
        /// @param channel_id The channel being acknowledged on.
        /// @param sequence_number The sequence number of the request being acknowledged.
        /// @param status The outcome; zero means accepted.
        /// @return Nothing, or the reason the datagram could not be encoded.
        [[nodiscard]] expected_void_t encode_tunnelling_ack_packet(span_uint8_t dest, std::uint8_t channel_id,
                                                                                        std::uint8_t sequence_number,
                                                                                        std::uint8_t status = 0u) noexcept;
        /// @brief Decodes a complete TUNNELLING_REQUEST datagram.
        /// @param buf The received octets, header included.
        /// @return The decoded request, or the reason it could not be read.
        [[nodiscard]] std::expected<tunnelling_request_frame, std::error_code> decode_tunnelling_request_packet(cspan_uint8_t buf) noexcept;
        /// @brief Decodes a complete TUNNELLING_ACK datagram.
        /// @param buf The received octets, header included.
        /// @return The decoded acknowledgement, or the reason it could not be read.
        [[nodiscard]] std::expected<tunnelling_ack_frame, std::error_code> decode_tunnelling_ack_packet(cspan_uint8_t buf) noexcept;
        /// @brief Encodes a complete DEVICE_CONFIGURATION_REQUEST datagram.
        /// @param dest The destination octets.
        /// @param channel_id The management channel to send on.
        /// @param sequence_number The sequence number to send under.
        /// @param cemi_bytes The cEMI management message to carry.
        /// @return Nothing, or the reason the datagram could not be encoded.
        [[nodiscard]] expected_void_t encode_device_configuration_request_packet(
            span_uint8_t dest, std::uint8_t channel_id, std::uint8_t sequence_number, cspan_uint8_t cemi_bytes) noexcept;
        /// @brief Decodes a complete DEVICE_CONFIGURATION_REQUEST datagram.
        /// @param buf The received octets, header included.
        /// @return The decoded request, or the reason it could not be read.
        /// @note The cEMI payload is not decoded; pass it to
        ///       @ref kmx::aio::knx::cemi::decode_property once its message code has been examined.
        [[nodiscard]] std::expected<device_configuration_frame, std::error_code> decode_device_configuration_request_packet(
            cspan_uint8_t buf) noexcept;
        /// @brief Encodes a complete DEVICE_CONFIGURATION_ACK datagram.
        /// @param dest The destination octets.
        /// @param channel_id The management channel being acknowledged on.
        /// @param sequence_number The sequence number of the request being acknowledged.
        /// @param status The outcome; zero means accepted.
        /// @return Nothing, or the reason the datagram could not be encoded.
        [[nodiscard]] expected_void_t encode_device_configuration_ack_packet(span_uint8_t dest, std::uint8_t channel_id,
                                                                            std::uint8_t sequence_number,
                                                                            std::uint8_t status = 0u) noexcept;
        /// @brief Decodes a complete DEVICE_CONFIGURATION_ACK datagram.
        /// @param buf The received octets, header included.
        /// @return The decoded acknowledgement, or the reason it could not be read.
        /// @note Shaped identically to a TUNNELLING_ACK, so it decodes to the same type.
        [[nodiscard]] std::expected<tunnelling_ack_frame, std::error_code> decode_device_configuration_ack_packet(
            cspan_uint8_t buf) noexcept;

        /// @brief Size of the feature identifier and return code octets that follow the connection header.
        inline constexpr std::size_t tunnelling_feature_header_size = 2u;

        /// @brief Indicates whether a service type names one of the four tunnelling feature services.
        [[nodiscard]] constexpr bool is_tunnelling_feature_service(const std::uint16_t value) noexcept
        {
            return (value == tunnelling_feature_get_service) || (value == tunnelling_feature_response_service) ||
                   (value == tunnelling_feature_set_service) || (value == tunnelling_feature_info_service);
        }

        /// @brief Encodes a complete tunnelling feature datagram, whichever of the four it is.
        /// @param dest The destination octets.
        /// @param value The frame to encode; its service type selects which of the four is produced.
        /// @param feature_value The value octets to carry; empty for a get, which carries none.
        /// @return Nothing, or the reason the datagram could not be encoded.
        /// @note The value is passed separately rather than taken from @ref tunnelling_feature_frame::value
        ///       so a caller can encode straight from octets it already holds.
        [[nodiscard]] expected_void_t encode_tunnelling_feature_packet(span_uint8_t dest, const tunnelling_feature_frame& value,
                                                                       cspan_uint8_t feature_value = {}) noexcept;
        /// @brief Decodes a complete tunnelling feature datagram, whichever of the four it is.
        /// @param buf The received octets, header included.
        /// @return The decoded frame, its service type naming which service arrived, or the reason it
        ///         could not be read.
        /// @retval kmx::aio::knx::error::invalid_length The feature value exceeds
        ///         @ref tunnelling_feature_value::capacity.
        [[nodiscard]] std::expected<tunnelling_feature_frame, std::error_code> decode_tunnelling_feature_packet(
            cspan_uint8_t buf) noexcept;
    }
}
#endif // KMX_AIO_FEATURE_KNX
