/// @file api/kmx/aio/knx/cemi.hpp
/// @brief Common External Message Interface (cEMI) L_Data encoding and decoding.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// cEMI is the medium-independent frame the KNXnet/IP services carry: a tunnelling request is a KNXnet/IP
/// header wrapped around exactly one cEMI message. This header implements the L_Data messages, which are
/// the ones that move group and point-to-point telegrams, and reports every other message code as
/// `error::unsupported_message_code` rather than guessing at its layout.
///
/// Wire layout of an L_Data message, all fields big-endian:
///
/// | Offset | Size | Field |
/// | :--- | :--- | :--- |
/// | 0 | 1 | message code |
/// | 1 | 1 | additional information length, usually zero |
/// | 2 | n | additional information, preserved but not interpreted |
/// | 2+n | 1 | control field 1 — frame type, repeat, broadcast, priority, ack, confirm |
/// | 3+n | 1 | control field 2 — address type, hop count, extended frame format |
/// | 4+n | 2 | source individual address |
/// | 6+n | 2 | destination address, read as group or individual per control field 2 |
/// | 8+n | 1 | data length — the APDU octet count minus one |
/// | 9+n | 2 | TPCI and APCI; the low six bits carry the value of a compact APDU |
/// | 11+n | len-1 | application data, absent when the APDU is compact |
///
/// Everything here is `constexpr`, allocation-free and free of I/O. Decoding never copies the payload: a
/// decoded frame stores the payload's offset and size, and @ref kmx::aio::knx::cemi_frame::payload maps
/// that back onto the buffer it was decoded from.
/// @reference KNX System Specifications, Volume 3/6/3 "EMI/IMI", cEMI L_Data.
/// @reference KNX System Specifications, Volume 3/3/7 "Application Layer", APCI codes.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/apdu_payload.hpp>
        #include <kmx/aio/knx/cemi_frame.hpp>
        #include <kmx/aio/knx/error.hpp>
        #include <kmx/aio/knx/group_address.hpp>
        #include <kmx/aio/knx/individual_address.hpp>
        #include <kmx/aio/knx/property_frame.hpp>

        #include <cstdint>
        #include <expected>
        #include <span>
    #endif

namespace kmx::aio::knx
{
    /// @brief The link layer flags of an outgoing L_Data frame.
    /// @details The defaults produce the control fields `0xBC` and `0xE0` that every interface expects
    ///          from a tunnelling client: standard frame, repetition allowed, broadcast, low priority,
    ///          group addressed, six hops.
    struct l_data_options
    {
        /// @brief The telegram priority.
        priority telegram_priority {priority::low};
        /// @brief The routing hop count, 0..7; seven disables hop counting.
        /// @note A larger value is rejected by the encoder rather than truncated to the field width.
        std::uint8_t hop_count {6u};
        /// @brief Whether a link layer acknowledgement is requested.
        bool acknowledge_request {false};
        /// @brief Whether the medium may repeat the frame after an error.
        /// @note This is control field 1 bit 5 as sent. In a received indication the same bit reads the
        ///       other way round: a cleared bit marks a frame that *is* a repetition.
        bool repeat {true};
        /// @brief Whether the frame is a domain broadcast rather than a system broadcast.
        bool broadcast {true};
    };

    /// @brief cEMI encode and decode operations.
    namespace cemi
    {
        /// @brief Size of the message code and additional information length octets.
        inline constexpr std::size_t prologue_size = 2u;
        /// @brief Size of the two control fields, two addresses and the data length octet.
        inline constexpr std::size_t link_header_size = 7u;
        /// @brief Size of the TPCI and APCI octet pair.
        inline constexpr std::size_t apci_size = 2u;
        /// @brief Smallest L_Data message: no additional information and a compact APDU.
        inline constexpr std::size_t min_l_data_size = prologue_size + link_header_size + apci_size;
        /// @brief Largest L_Data message this build accepts, with no additional information.
        /// @warning This is the size of a message an *encoder* here produces, which never emits an
        ///          additional information block. It is not an upper bound on a *decoded* message: a peer
        ///          may send up to @ref max_additional_info_size octets of additional information ahead of
        ///          the link header. Size a buffer that receives decoded octets with @ref max_message_size.
        inline constexpr std::size_t max_l_data_size = min_l_data_size + apdu_payload::max_octets;
        /// @brief Largest additional information block, the range of the one-octet length field.
        inline constexpr std::size_t max_additional_info_size = 0xFFu;
        /// @brief Largest L_Data message that can reach the decoder at all.
        /// @details Both variable fields at their maximum: a full additional information block and the
        ///          longest APDU the data length octet can describe. Nothing @ref decode accepts is larger,
        ///          which the static assertion below pins to the length arithmetic rather than to this sum.
        inline constexpr std::size_t max_message_size = max_l_data_size + max_additional_info_size;
        /// @brief Largest data length field a standard frame may carry; longer frames must be extended.
        inline constexpr std::uint8_t max_standard_data_length = 0x0Fu;
        /// @brief Transport layer control value of an unnumbered data packet, which is what group traffic uses.
        inline constexpr std::uint8_t tpci_unnumbered_data = 0x00u;
        /// @brief Mask of the transport control bits within the first APCI octet.
        inline constexpr std::uint8_t tpci_mask = 0xFCu;
        /// @brief Mask of the four-bit application service code within a ten-bit APCI value.
        inline constexpr std::uint16_t apci_code_mask = 0x03C0u;
        /// @brief Application service code that escapes to a ten-bit user memory service.
        inline constexpr std::uint16_t apci_escape_user = 0x02C0u;
        /// @brief Application service code that escapes to a ten-bit management service.
        inline constexpr std::uint16_t apci_escape_management = 0x03C0u;

        /// @brief Returns the encoded size of an L_Data message.
        /// @param additional_info_size The size of the additional information block.
        /// @param data_length The wire data length field.
        /// @return The total number of octets the message occupies.
        [[nodiscard]] constexpr std::size_t encoded_size(const std::size_t additional_info_size, const std::uint8_t data_length) noexcept
        {
            return prologue_size + additional_info_size + link_header_size + 1u + data_length;
        }

        // Both variable fields are one octet wide, so this is the largest size the length arithmetic above
        // can produce - and therefore the largest message decode() can accept. A buffer sized to hold a
        // decoded message must be at least this large; max_l_data_size is 255 octets short of it.
        static_assert(encoded_size(max_additional_info_size, 0xFFu) == max_message_size,
                      "max_message_size must bound every size encoded_size can return");

        /// @brief Indicates whether a message code names an L_Data message.
        /// @param code The message code to test.
        /// @return `true` for the request, confirmation and indication codes.
        [[nodiscard]] constexpr bool is_l_data(const cemi_message_code code) noexcept
        {
            return (code == cemi_message_code::l_data_req) || (code == cemi_message_code::l_data_con) ||
                   (code == cemi_message_code::l_data_ind);
        }

        /// @brief Size of a device management property service header, message code included.
        /// @details Message code, interface object type (2), object instance, property id, then the element
        ///          count and start index packed into one 16-bit field.
        inline constexpr std::size_t property_header_size = 7u;
        /// @brief Largest element count a property service can name; the field is four bits.
        inline constexpr std::uint8_t max_property_elements = 0x0Fu;
        /// @brief Largest start index a property service can name; the field is twelve bits.
        inline constexpr std::uint16_t max_property_start_index = 0x0FFFu;

        /// @brief Indicates whether a message code names a device management property service.
        [[nodiscard]] constexpr bool is_property_service(const cemi_message_code code) noexcept
        {
            return (code == cemi_message_code::m_prop_read_req) || (code == cemi_message_code::m_prop_read_con) ||
                   (code == cemi_message_code::m_prop_write_req) || (code == cemi_message_code::m_prop_write_con) ||
                   (code == cemi_message_code::m_prop_info_ind);
        }

        /// @brief Indicates whether a message code names a device management reset service.
        [[nodiscard]] constexpr bool is_reset(const cemi_message_code code) noexcept
        {
            return (code == cemi_message_code::m_reset_req) || (code == cemi_message_code::m_reset_ind);
        }

        /// @brief Encodes one cEMI device management property service.
        /// @param dest The buffer to write into.
        /// @param value The service to encode; its data offset and size are ignored.
        /// @param data The property data; empty for a read request.
        /// @return The number of octets written, or the reason the message could not be encoded.
        [[nodiscard]] constexpr std::expected<std::size_t, error> encode_property(const span_uint8_t dest, const property_frame& value,
                                                                                  const cspan_uint8_t data = {}) noexcept
        {
            if (!is_property_service(value.message_code))
                return std::unexpected(error::unsupported_message_code);
            if ((value.element_count > max_property_elements) || (value.start_index > max_property_start_index))
                return std::unexpected(error::invalid_configuration);
            if (data.size() > apdu_payload::max_octets)
                return std::unexpected(error::payload_too_large);

            const auto size = property_header_size + data.size();
            if (dest.size() < size)
                return std::unexpected(error::invalid_length);

            dest[0u] = static_cast<std::uint8_t>(value.message_code);
            dest[1u] = static_cast<std::uint8_t>(value.object_type >> 8u);
            dest[2u] = static_cast<std::uint8_t>(value.object_type & 0xFFu);
            dest[3u] = value.object_instance;
            dest[4u] = value.property_id;
            dest[5u] = static_cast<std::uint8_t>((value.element_count << 4u) | ((value.start_index >> 8u) & 0x0Fu));
            dest[6u] = static_cast<std::uint8_t>(value.start_index & 0xFFu);

            for (std::size_t i {}; i < data.size(); ++i)
                dest[property_header_size + i] = data[i];

            return size;
        }

        /// @brief The run of property elements a device management service reads or writes.
        struct property_selector
        {
            /// @brief The interface object type.
            std::uint16_t object_type {};
            /// @brief Which instance of that type, counted from one.
            std::uint8_t object_instance {};
            /// @brief The property.
            std::uint8_t property_id {};
            /// @brief How many elements the service covers.
            std::uint8_t element_count {1u};
            /// @brief The first element, counted from one.
            std::uint16_t start_index {1u};
        };

        /// @brief Encodes an M_PropRead.req.
        /// @param dest The buffer to write into.
        /// @param property The interface object, its instance, the property and the elements to read.
        /// @return The number of octets written, or the reason it could not be encoded.
        [[nodiscard]] constexpr std::expected<std::size_t, error> encode_property_read(const span_uint8_t dest,
                                                                                       const property_selector& property) noexcept
        {
            return encode_property(dest, property_frame {cemi_message_code::m_prop_read_req, property.object_type, property.object_instance,
                                                         property.property_id, property.element_count, property.start_index});
        }

        /// @brief Encodes an M_PropWrite.req.
        /// @param dest The buffer to write into.
        /// @param property The interface object, its instance, the property and the elements the value covers.
        /// @param data The value to write.
        /// @return The number of octets written, or the reason it could not be encoded.
        [[nodiscard]] constexpr std::expected<std::size_t, error> encode_property_write(const span_uint8_t dest,
                                                                                        const property_selector& property,
                                                                                        const cspan_uint8_t data) noexcept
        {
            return encode_property(dest,
                                   property_frame {cemi_message_code::m_prop_write_req, property.object_type, property.object_instance,
                                                   property.property_id, property.element_count, property.start_index},
                                   data);
        }

        /// @brief Encodes an M_Reset.req, which carries nothing but its message code.
        /// @param dest The buffer to write into.
        /// @param code The reset code; must be a reset service.
        /// @return The number of octets written, or the reason it could not be encoded.
        [[nodiscard]] constexpr std::expected<std::size_t, error> encode_reset(
            const span_uint8_t dest, const cemi_message_code code = cemi_message_code::m_reset_req) noexcept
        {
            if (!is_reset(code))
                return std::unexpected(error::unsupported_message_code);
            if (dest.empty())
                return std::unexpected(error::invalid_length);

            dest[0u] = static_cast<std::uint8_t>(code);
            return std::size_t {1u};
        }

        /// @brief Decodes one cEMI device management property service.
        /// @param bytes The message octets; the data is named by offset into this very buffer.
        /// @return The decoded message, or the reason it could not be decoded.
        [[nodiscard]] constexpr std::expected<property_frame, error> decode_property(const cspan_uint8_t bytes) noexcept
        {
            if (bytes.empty())
                return std::unexpected(error::malformed_frame);

            const auto code = static_cast<cemi_message_code>(bytes[0u]);
            if (!is_property_service(code))
                return std::unexpected(error::unsupported_message_code);
            if (bytes.size() < property_header_size)
                return std::unexpected(error::malformed_frame);

            property_frame decoded {};
            decoded.message_code = code;
            decoded.object_type = static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[1u]) << 8u) | bytes[2u]);
            decoded.object_instance = bytes[3u];
            decoded.property_id = bytes[4u];
            decoded.element_count = static_cast<std::uint8_t>((bytes[5u] >> 4u) & 0x0Fu);
            decoded.start_index = static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[5u] & 0x0Fu) << 8u) | bytes[6u]);

            const auto data_size = bytes.size() - property_header_size;
            if (data_size != 0u)
            {
                decoded.data_offset = static_cast<std::uint16_t>(property_header_size);
                decoded.data_size = static_cast<std::uint16_t>(data_size);
            }

            return decoded;
        }

        /// @brief Identifies the application service a ten-bit APCI value names.
        /// @param raw The ten-bit value read from the APCI octets.
        /// @return The service, with the compact value masked off for the services that carry one.
        [[nodiscard]] constexpr apci service_of(const std::uint16_t raw) noexcept
        {
            const auto code = static_cast<std::uint16_t>(raw & apci_code_mask);
            const auto escaped = (code == apci_escape_user) || (code == apci_escape_management);
            return static_cast<apci>(escaped ? raw : code);
        }

        /// @brief Builds control field 1 from the link layer flags.
        /// @param options The flags to encode.
        /// @param data_length The wire data length field; decides the frame type.
        /// @return The control field 1 octet.
        /// @note The frame type follows the length rather than the caller: a standard frame cannot carry a
        ///       data length above @ref max_standard_data_length, so a longer message is marked extended.
        [[nodiscard]] constexpr std::uint8_t make_control_field_1(const l_data_options& options, const std::uint8_t data_length) noexcept
        {
            std::uint8_t value {};
            if (data_length <= max_standard_data_length)
                value |= cemi_frame::standard_frame_mask;
            if (options.repeat)
                value |= cemi_frame::repeat_mask;
            if (options.broadcast)
                value |= cemi_frame::broadcast_mask;
            value |= static_cast<std::uint8_t>((static_cast<std::uint8_t>(options.telegram_priority) << 2u) & cemi_frame::priority_mask);
            if (options.acknowledge_request)
                value |= cemi_frame::acknowledge_mask;
            return value;
        }

        /// @brief Builds control field 2 from the addressing mode and hop count.
        /// @param destination_type How the destination address is to be read.
        /// @param hop_count The routing hop count, 0..7; the encoder rejects anything larger.
        /// @return The control field 2 octet.
        [[nodiscard]] constexpr std::uint8_t make_control_field_2(const knx::address_type destination_type,
                                                                  const std::uint8_t hop_count) noexcept
        {
            std::uint8_t value {};
            if (destination_type == knx::address_type::group)
                value |= cemi_frame::address_type_mask;
            value |= static_cast<std::uint8_t>((hop_count << 4u) & cemi_frame::hop_count_mask);
            return value;
        }

        /// @brief One L_Data message to encode: who sends it where, the service it carries, and how the link layer treats it.
        struct l_data_message
        {
            /// @brief The message code; must be one of the L_Data codes.
            cemi_message_code code {cemi_message_code::l_data_req};
            /// @brief The sending device; a tunnelling client may leave this unset and let the interface substitute its own
            ///        address.
            individual_address source {};
            /// @brief The raw destination address.
            std::uint16_t destination {};
            /// @brief How the destination address is to be read.
            knx::address_type destination_type {knx::address_type::group};
            /// @brief The application layer service.
            apci service {};
            /// @brief The application payload.
            apdu_payload payload {};
            /// @brief The link layer flags.
            l_data_options options {};
        };

        /// @brief Writes the octets of one L_Data message into a buffer already known to be large enough.
        /// @param dest The buffer to write into.
        /// @param message The message.
        /// @param data_length The wire data length field, as the payload reported it.
        /// @note Every bound is the caller's to check; nothing here rejects anything.
        constexpr void write_l_data(const span_uint8_t dest, const l_data_message& message, const std::uint8_t data_length) noexcept
        {
            const auto raw_service = static_cast<std::uint16_t>(message.service);
            dest[0u] = static_cast<std::uint8_t>(message.code);
            dest[1u] = 0u;
            dest[2u] = make_control_field_1(message.options, data_length);
            dest[3u] = make_control_field_2(message.destination_type, message.options.hop_count);
            dest[4u] = static_cast<std::uint8_t>(message.source.value() >> 8u);
            dest[5u] = static_cast<std::uint8_t>(message.source.value() & 0xFFu);
            dest[6u] = static_cast<std::uint8_t>(message.destination >> 8u);
            dest[7u] = static_cast<std::uint8_t>(message.destination & 0xFFu);
            dest[8u] = data_length;
            dest[9u] = static_cast<std::uint8_t>(tpci_unnumbered_data | ((raw_service >> 8u) & 0x03u));
            dest[10u] = static_cast<std::uint8_t>(raw_service & 0xFFu);

            // A compact APDU rides in the low six bits of the service octet; anything longer follows it.
            if (message.payload.compacted())
                dest[10u] = static_cast<std::uint8_t>(dest[10u] | message.payload.compact_value());
            else
            {
                const auto octets = message.payload.octets();
                for (std::size_t i {}; i < octets.size(); ++i)
                    dest[min_l_data_size + i] = octets[i];
            }
        }

        /// @brief Encodes one cEMI L_Data message.
        /// @param dest The buffer to write into.
        /// @param message The message; its code must be one of the L_Data codes.
        /// @return The number of octets written, or the reason the message could not be encoded.
        [[nodiscard]] constexpr std::expected<std::size_t, error> encode(const span_uint8_t dest, const l_data_message& message) noexcept
        {
            if (!is_l_data(message.code))
                return std::unexpected(error::unsupported_message_code);
            if (message.payload.octets().size() > apdu_payload::max_octets)
                return std::unexpected(error::payload_too_large);
            if (message.options.hop_count > 0x07u)
                return std::unexpected(error::invalid_configuration);

            const auto data_length = message.payload.data_length();
            const auto size = encoded_size(0u, data_length);
            if (dest.size() < size)
                return std::unexpected(error::invalid_length);

            write_l_data(dest, message, data_length);
            return size;
        }

        /// @brief One group telegram to encode: the group, the value, the sender and the link layer flags.
        struct group_telegram
        {
            /// @brief The destination group.
            group_address destination {};
            /// @brief The value, as encoded by the datapoint layer.
            apdu_payload payload {};
            /// @brief The sending device; unset lets the interface substitute its own address.
            individual_address source {};
            /// @brief The link layer flags.
            l_data_options options {};
        };

        /// @brief Encodes an A_GroupValue_Write request.
        /// @param dest The buffer to write into.
        /// @param telegram The group to write to, the value to write, the sender and the link layer flags.
        /// @return The number of octets written, or the reason the message could not be encoded.
        [[nodiscard]] constexpr std::expected<std::size_t, error> encode_group_value_write(const span_uint8_t dest,
                                                                                           const group_telegram& telegram) noexcept
        {
            return encode(dest, l_data_message {.source = telegram.source,
                                                .destination = telegram.destination.value(),
                                                .service = apci::group_value_write,
                                                .payload = telegram.payload,
                                                .options = telegram.options});
        }

        /// @brief Encodes an A_GroupValue_Read request.
        /// @param dest The buffer to write into.
        /// @param destination The destination group.
        /// @param source The sending device; unset lets the interface substitute its own address.
        /// @param options The link layer flags.
        /// @return The number of octets written, or the reason the message could not be encoded.
        [[nodiscard]] constexpr std::expected<std::size_t, error> encode_group_value_read(const span_uint8_t dest,
                                                                                          const group_address destination,
                                                                                          const individual_address source = {},
                                                                                          const l_data_options& options = {}) noexcept
        {
            return encode(dest,
                          l_data_message {
                              .source = source, .destination = destination.value(), .service = apci::group_value_read, .options = options});
        }

        /// @brief Encodes an A_GroupValue_Response.
        /// @param dest The buffer to write into.
        /// @param telegram The group the response belongs to, the value to report, the sender and the link layer flags.
        /// @return The number of octets written, or the reason the message could not be encoded.
        [[nodiscard]] constexpr std::expected<std::size_t, error> encode_group_value_response(const span_uint8_t dest,
                                                                                              const group_telegram& telegram) noexcept
        {
            return encode(dest, l_data_message {.source = telegram.source,
                                                .destination = telegram.destination.value(),
                                                .service = apci::group_value_response,
                                                .payload = telegram.payload,
                                                .options = telegram.options});
        }

        /// @brief Reads the message code of a cEMI message without decoding it.
        /// @param bytes The message octets.
        /// @return The message code, or `error::malformed_frame` for an empty buffer.
        [[nodiscard]] constexpr std::expected<cemi_message_code, error> message_code(const cspan_uint8_t bytes) noexcept
        {
            if (bytes.empty())
                return std::unexpected(error::malformed_frame);

            return static_cast<cemi_message_code>(bytes[0u]);
        }

        /// @brief Reads the link layer header of an L_Data message into a frame.
        /// @param bytes The message's octets.
        /// @param link_offset Where the link header starts, past any additional information.
        /// @param code The message code already read from the prologue.
        /// @param additional_info_length The additional information length already read from the prologue.
        /// @return The frame with its link layer fields filled in; the application service is the
        ///         caller's to add, since reading it needs the length the caller already validated.
        [[nodiscard]] constexpr cemi_frame read_link_header(const cspan_uint8_t bytes, const std::size_t link_offset,
                                                            const cemi_message_code code,
                                                            const std::uint8_t additional_info_length) noexcept
        {
            cemi_frame decoded {};
            decoded.message_code = code;
            decoded.additional_info_length = additional_info_length;
            decoded.control_field_1 = bytes[link_offset];
            decoded.control_field_2 = bytes[link_offset + 1u];
            decoded.source = individual_address {
                static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[link_offset + 2u]) << 8u) | bytes[link_offset + 3u])};
            decoded.destination =
                static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[link_offset + 4u]) << 8u) | bytes[link_offset + 5u]);
            decoded.data_length = bytes[link_offset + 6u];
            decoded.transport_control = static_cast<std::uint8_t>(bytes[link_offset + 7u] & tpci_mask);
            return decoded;
        }

        /// @brief Decodes one cEMI L_Data message.
        /// @param bytes The message octets; the payload is named by offset into this very buffer.
        /// @return The decoded message, or the reason it could not be decoded.
        /// @note The declared data length must account for the buffer exactly. A frame whose length field
        ///       disagrees with the octets that carry it is rejected rather than truncated silently.
        [[nodiscard]] constexpr std::expected<cemi_frame, error> decode(const cspan_uint8_t bytes) noexcept
        {
            if (bytes.size() < prologue_size)
                return std::unexpected(error::malformed_frame);

            const auto code = static_cast<cemi_message_code>(bytes[0u]);
            if (!is_l_data(code))
                return std::unexpected(error::unsupported_message_code);

            const auto additional_info_length = bytes[1u];
            const std::size_t link_offset = prologue_size + additional_info_length;
            if ((link_offset + link_header_size + apci_size) > bytes.size())
                return std::unexpected(error::malformed_frame);

            const auto data_length = bytes[link_offset + 6u];
            // A zero length names a transport layer control APDU - T_Connect and its relatives - which
            // carries no application service at all. Point-to-point connection management is out of this
            // build's scope, so such a frame is reported as unsupported rather than decoded as if it
            // held a service it does not have.
            if (data_length == 0u)
                return std::unexpected(error::unsupported_service);
            if (encoded_size(additional_info_length, data_length) != bytes.size())
                return std::unexpected(error::invalid_length);

            const auto raw_service =
                static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[link_offset + 7u] & 0x03u) << 8u) | bytes[link_offset + 8u]);

            auto decoded = read_link_header(bytes, link_offset, code, additional_info_length);
            decoded.application_service = service_of(raw_service);

            if (data_length == 1u)
                decoded.compact_value = static_cast<std::uint8_t>(raw_service & apdu_payload::compact_mask);
            else
            {
                decoded.payload_offset = static_cast<std::uint16_t>(link_offset + link_header_size + apci_size);
                decoded.payload_size = static_cast<std::uint16_t>(data_length - 1u);
            }

            return decoded;
        }

        /// @brief Returns the additional information block of a decoded message.
        /// @param decoded The decoded message.
        /// @param bytes The very buffer it was decoded from.
        /// @return The additional information octets, empty when there are none.
        [[nodiscard]] constexpr cspan_uint8_t additional_info(const cemi_frame& decoded, const cspan_uint8_t bytes) noexcept
        {
            if ((decoded.additional_info_length == 0u) || ((prologue_size + decoded.additional_info_length) > bytes.size()))
                return {};

            return bytes.subspan(prologue_size, decoded.additional_info_length);
        }
    }
}
#endif // KMX_AIO_FEATURE_KNX
