/// @file aio/knx/cemi.hpp
/// @brief Common External Message Interface (cEMI) L_Data encoding and decoding.
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
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <expected>
    #include <span>
#endif

#include <kmx/aio/basic_types.hpp>
#include <kmx/aio/knx/address.hpp>
#include <kmx/aio/knx/error.hpp>

namespace kmx::aio::knx
{
    /// @brief cEMI message codes.
    /// @note The three L_Data codes are the only ones this build encodes or decodes; the remaining
    ///       entries exist so a received message can be named in a log instead of being reported as a
    ///       malformed frame.
    enum class cemi_message_code : std::uint8_t
    {
        /// @brief L_Busmon.ind — a bus monitor frame.
        l_busmon_ind = 0x2Bu,
        /// @brief L_Raw.req — a raw medium frame.
        l_raw_req = 0x10u,
        /// @brief L_Raw.ind — a raw medium frame.
        l_raw_ind = 0x2Du,
        /// @brief L_Raw.con — a raw medium confirmation.
        l_raw_con = 0x2Fu,
        /// @brief L_Data.req — a link layer request, client to interface.
        l_data_req = 0x11u,
        /// @brief L_Data.con — the interface's confirmation of a request it sent to the bus.
        l_data_con = 0x2Eu,
        /// @brief L_Data.ind — a link layer indication, interface to client.
        l_data_ind = 0x29u,
        /// @brief L_Poll_Data.req — a polling request.
        l_poll_data_req = 0x13u,
        /// @brief L_Poll_Data.con — a polling confirmation.
        l_poll_data_con = 0x25u,
        /// @brief M_PropRead.req — a device management property read.
        m_prop_read_req = 0xFCu,
        /// @brief M_PropRead.con — a device management property read response.
        m_prop_read_con = 0xFBu,
        /// @brief M_PropWrite.req — a device management property write.
        m_prop_write_req = 0xF6u,
        /// @brief M_PropWrite.con — a device management property write response.
        m_prop_write_con = 0xF5u,
        /// @brief M_PropInfo.ind — an unsolicited device management indication.
        m_prop_info_ind = 0xF7u,
        /// @brief M_Reset.req — a device management reset request.
        m_reset_req = 0xF1u,
        /// @brief M_Reset.ind — a device management reset indication.
        m_reset_ind = 0xF0u,
    };

    /// @brief Telegram priority, control field 1 bits 3..2.
    enum class priority : std::uint8_t
    {
        /// @brief System priority — reserved for management traffic.
        system = 0u,
        /// @brief Normal priority.
        normal = 1u,
        /// @brief Urgent priority — alarms.
        urgent = 2u,
        /// @brief Low priority — the default for ordinary group traffic.
        low = 3u,
    };

    /// @brief How the destination address of an L_Data frame is to be read.
    enum class address_type : std::uint8_t
    {
        /// @brief The destination is one device — point-to-point communication.
        individual = 0u,
        /// @brief The destination is a group — the usual case.
        group = 1u,
    };

    /// @brief Application layer service identifiers, as ten-bit APCI values.
    /// @note Most services are identified by their upper four bits alone, and the lower six bits then
    ///       belong to the service's own parameter or compact value. The two codes @ref
    ///       kmx::aio::knx::cemi::apci_escape_user and @ref kmx::aio::knx::cemi::apci_escape_management
    ///       are the exception: under those, all ten bits name the service.
    enum class apci : std::uint16_t
    {
        /// @brief A_GroupValue_Read — ask the group for its current value.
        group_value_read = 0x000u,
        /// @brief A_GroupValue_Response — the answer to a read.
        group_value_response = 0x040u,
        /// @brief A_GroupValue_Write — set the group's value.
        group_value_write = 0x080u,
        /// @brief A_IndividualAddress_Write — assign an individual address in programming mode.
        individual_address_write = 0x0C0u,
        /// @brief A_IndividualAddress_Read — ask devices in programming mode for their address.
        individual_address_read = 0x100u,
        /// @brief A_IndividualAddress_Response — the answer to an individual address read.
        individual_address_response = 0x140u,
        /// @brief A_ADC_Read — read an analogue-to-digital converter channel.
        adc_read = 0x180u,
        /// @brief A_ADC_Response — the answer to an ADC read.
        adc_response = 0x1C0u,
        /// @brief A_Memory_Read — read device memory.
        memory_read = 0x200u,
        /// @brief A_Memory_Response — the answer to a memory read.
        memory_response = 0x240u,
        /// @brief A_Memory_Write — write device memory.
        memory_write = 0x280u,
        /// @brief A_UserMemory_Read — read user memory.
        user_memory_read = 0x2C0u,
        /// @brief A_UserMemory_Response — the answer to a user memory read.
        user_memory_response = 0x2C1u,
        /// @brief A_UserMemory_Write — write user memory.
        user_memory_write = 0x2C2u,
        /// @brief A_UserManufacturerInfo_Read — read the manufacturer information block.
        user_manufacturer_info_read = 0x2C5u,
        /// @brief A_UserManufacturerInfo_Response — the answer to a manufacturer information read.
        user_manufacturer_info_response = 0x2C6u,
        /// @brief A_DeviceDescriptor_Read — read a device descriptor.
        device_descriptor_read = 0x300u,
        /// @brief A_DeviceDescriptor_Response — the answer to a device descriptor read.
        device_descriptor_response = 0x340u,
        /// @brief A_Restart — restart the device.
        restart = 0x380u,
        /// @brief A_PropertyValue_Read — read an interface object property.
        property_value_read = 0x3D5u,
        /// @brief A_PropertyValue_Response — the answer to a property read.
        property_value_response = 0x3D6u,
        /// @brief A_PropertyValue_Write — write an interface object property.
        property_value_write = 0x3D7u,
        /// @brief A_PropertyDescription_Read — read a property description.
        property_description_read = 0x3D8u,
        /// @brief A_PropertyDescription_Response — the answer to a property description read.
        property_description_response = 0x3D9u,
    };

    /// @brief An application protocol data unit payload.
    /// @details A KNX APDU carries its value either in the six spare bits of the APCI octet — the compact
    ///          form every one-bit and four-bit datapoint uses — or in whole octets after it. The two are
    ///          different encodings of the same field, and which one applies follows from the datapoint
    ///          type rather than from the value, so the choice is made here and never guessed at.
    /// @warning The extended form is a view. The octets it names must outlive every encode call that uses
    ///          it; nothing is copied.
    class apdu_payload
    {
    public:
        /// @brief Largest number of payload octets an APDU can carry.
        /// @details The data length field is one octet and counts the APDU minus its first octet.
        static constexpr std::size_t max_octets = 254u;
        /// @brief Mask of the bits a compact payload occupies in the APCI octet.
        static constexpr std::uint8_t compact_mask = 0x3Fu;

        /// @brief Creates the compact payload with value zero, as used by A_GroupValue_Read.
        constexpr apdu_payload() noexcept = default;

        /// @brief Creates a compact payload carried inside the APCI octet.
        /// @param value The six-bit value; higher bits are discarded.
        /// @return The payload.
        [[nodiscard]] static constexpr apdu_payload compact(const std::uint8_t value) noexcept
        {
            apdu_payload result {};
            result.compact_value_ = static_cast<std::uint8_t>(value & compact_mask);
            return result;
        }

        /// @brief Creates a payload carried in whole octets after the APCI octet.
        /// @param octets The payload octets; borrowed, not copied.
        /// @return The payload, or `error::payload_too_large` when it exceeds @ref max_octets.
        /// @note An empty octet span yields the compact payload with value zero, because an APDU always
        ///       carries at least the APCI octet and therefore has no zero-length form.
        [[nodiscard]] static constexpr std::expected<apdu_payload, error> extended(const cspan_uint8_t octets) noexcept
        {
            if (octets.size() > max_octets)
                return std::unexpected(error::payload_too_large);
            if (octets.empty())
                return apdu_payload {};

            apdu_payload result {};
            result.octets_ = octets;
            result.compacted_ = false;
            return result;
        }

        /// @brief Indicates whether the value sits in the six spare bits of the APCI octet.
        [[nodiscard]] constexpr bool compacted() const noexcept { return compacted_; }
        /// @brief Returns the six-bit value; zero unless the payload is compact.
        [[nodiscard]] constexpr std::uint8_t compact_value() const noexcept { return compact_value_; }
        /// @brief Returns the payload octets; empty unless the payload is extended.
        [[nodiscard]] constexpr cspan_uint8_t octets() const noexcept { return octets_; }

        /// @brief Returns the wire data length field: the APDU octet count minus one.
        [[nodiscard]] constexpr std::uint8_t data_length() const noexcept
        {
            return compacted_ ? std::uint8_t {1u} : static_cast<std::uint8_t>(octets_.size() + 1u);
        }

    private:
        /// @brief The borrowed payload octets of an extended payload.
        cspan_uint8_t octets_ {};
        /// @brief The six-bit value of a compact payload.
        std::uint8_t compact_value_ {};
        /// @brief Whether the value sits in the APCI octet.
        bool compacted_ {true};
    };

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

    /// @brief A decoded cEMI L_Data message.
    /// @details Trivially copyable and self-contained apart from the payload, which is named by offset and
    ///          size so that copying a decoded frame can never leave a dangling view behind.
    struct cemi_frame
    {
        /// @brief The message code.
        cemi_message_code message_code {cemi_message_code::l_data_ind};
        /// @brief The length of the additional information block, usually zero.
        std::uint8_t additional_info_length {};
        /// @brief Control field 1 — frame type, repeat, broadcast, priority, acknowledge, confirm.
        std::uint8_t control_field_1 {};
        /// @brief Control field 2 — address type, hop count, extended frame format.
        std::uint8_t control_field_2 {};
        /// @brief The sending device.
        individual_address source {};
        /// @brief The raw destination address; read it with @ref group_destination or @ref individual_destination.
        std::uint16_t destination {};
        /// @brief The wire data length field: the APDU octet count minus one.
        std::uint8_t data_length {};
        /// @brief The transport layer control bits of the first APDU octet.
        std::uint8_t transport_control {};
        /// @brief The application layer service.
        apci application_service {};
        /// @brief The six-bit value of a compact APDU; zero for an extended one.
        std::uint8_t compact_value {};
        /// @brief The offset of the payload octets within the decoded buffer; zero for a compact APDU.
        std::uint16_t payload_offset {};
        /// @brief The number of payload octets; zero for a compact APDU.
        std::uint16_t payload_size {};

        /// @brief Bit mask of the frame type flag in control field 1.
        static constexpr std::uint8_t standard_frame_mask = 0x80u;
        /// @brief Bit mask of the repeat flag in control field 1.
        static constexpr std::uint8_t repeat_mask = 0x20u;
        /// @brief Bit mask of the broadcast flag in control field 1.
        static constexpr std::uint8_t broadcast_mask = 0x10u;
        /// @brief Bit mask of the priority field in control field 1.
        static constexpr std::uint8_t priority_mask = 0x0Cu;
        /// @brief Bit mask of the acknowledge request flag in control field 1.
        static constexpr std::uint8_t acknowledge_mask = 0x02u;
        /// @brief Bit mask of the confirm flag in control field 1.
        static constexpr std::uint8_t confirm_mask = 0x01u;
        /// @brief Bit mask of the address type flag in control field 2.
        static constexpr std::uint8_t address_type_mask = 0x80u;
        /// @brief Bit mask of the hop count field in control field 2.
        static constexpr std::uint8_t hop_count_mask = 0x70u;
        /// @brief Bit mask of the extended frame format field in control field 2.
        static constexpr std::uint8_t extended_format_mask = 0x0Fu;

        /// @brief Indicates whether the frame is a standard rather than an extended one.
        [[nodiscard]] constexpr bool standard_frame() const noexcept { return (control_field_1 & standard_frame_mask) != 0u; }
        /// @brief Returns control field 1 bit 5 as sent.
        /// @note For an L_Data.ind a cleared bit marks a frame that is a repetition of an earlier one.
        [[nodiscard]] constexpr bool repeat_flag() const noexcept { return (control_field_1 & repeat_mask) != 0u; }
        /// @brief Indicates whether an L_Data.ind is a repetition of an earlier frame.
        [[nodiscard]] constexpr bool repetition() const noexcept { return !repeat_flag(); }
        /// @brief Indicates whether the frame is a domain broadcast rather than a system broadcast.
        [[nodiscard]] constexpr bool broadcast() const noexcept { return (control_field_1 & broadcast_mask) != 0u; }
        /// @brief Returns the telegram priority.
        [[nodiscard]] constexpr priority telegram_priority() const noexcept
        {
            return static_cast<priority>((control_field_1 & priority_mask) >> 2u);
        }
        /// @brief Indicates whether a link layer acknowledgement was requested.
        [[nodiscard]] constexpr bool acknowledge_requested() const noexcept { return (control_field_1 & acknowledge_mask) != 0u; }
        /// @brief Indicates whether a confirmation reports an error.
        /// @note Only meaningful on an L_Data.con.
        [[nodiscard]] constexpr bool confirm_error() const noexcept { return (control_field_1 & confirm_mask) != 0u; }
        /// @brief Returns how the destination address is to be read.
        [[nodiscard]] constexpr knx::address_type address_type() const noexcept
        {
            return ((control_field_2 & address_type_mask) != 0u) ? knx::address_type::group : knx::address_type::individual;
        }
        /// @brief Returns the remaining routing hops, 0..7.
        [[nodiscard]] constexpr std::uint8_t hop_count() const noexcept
        {
            return static_cast<std::uint8_t>((control_field_2 & hop_count_mask) >> 4u);
        }
        /// @brief Returns the extended frame format field, zero for a standard frame.
        [[nodiscard]] constexpr std::uint8_t extended_frame_format() const noexcept
        {
            return static_cast<std::uint8_t>(control_field_2 & extended_format_mask);
        }
        /// @brief Indicates whether the destination is a group address.
        [[nodiscard]] constexpr bool group_addressed() const noexcept { return address_type() == knx::address_type::group; }
        /// @brief Returns the destination read as a group address.
        [[nodiscard]] constexpr group_address group_destination() const noexcept { return group_address {destination}; }
        /// @brief Returns the destination read as an individual address.
        [[nodiscard]] constexpr individual_address individual_destination() const noexcept { return individual_address {destination}; }
        /// @brief Indicates whether the value sits in the six spare bits of the APCI octet.
        [[nodiscard]] constexpr bool compact() const noexcept { return payload_size == 0u; }
        /// @brief Indicates whether the APDU is an unnumbered data packet, which is what group traffic uses.
        [[nodiscard]] constexpr bool unnumbered() const noexcept { return (transport_control & 0xC0u) == 0u; }
        /// @brief Indicates whether the APDU belongs to a numbered point-to-point connection.
        [[nodiscard]] constexpr bool numbered() const noexcept { return (transport_control & 0xC0u) == 0x40u; }
        /// @brief Returns the transport layer sequence number of a numbered APDU, 0..15.
        [[nodiscard]] constexpr std::uint8_t sequence_number() const noexcept
        {
            return static_cast<std::uint8_t>((transport_control & 0x3Cu) >> 2u);
        }

        /// @brief Maps the payload back onto the buffer the frame was decoded from.
        /// @param bytes The very buffer that was decoded; a different one yields an empty payload.
        /// @return The payload octets, empty for a compact APDU.
        [[nodiscard]] constexpr cspan_uint8_t payload(const cspan_uint8_t bytes) const noexcept
        {
            if ((payload_size == 0u) || ((static_cast<std::size_t>(payload_offset) + payload_size) > bytes.size()))
                return {};

            return bytes.subspan(payload_offset, payload_size);
        }
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
        inline constexpr std::size_t max_l_data_size = min_l_data_size + apdu_payload::max_octets;
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

        /// @brief Indicates whether a message code names an L_Data message.
        /// @param code The message code to test.
        /// @return `true` for the request, confirmation and indication codes.
        [[nodiscard]] constexpr bool is_l_data(const cemi_message_code code) noexcept
        {
            return (code == cemi_message_code::l_data_req) || (code == cemi_message_code::l_data_con) ||
                   (code == cemi_message_code::l_data_ind);
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

        /// @brief Encodes one cEMI L_Data message.
        /// @param dest The buffer to write into.
        /// @param code The message code; must be one of the L_Data codes.
        /// @param source The sending device; a tunnelling client may leave this unset and let the
        ///        interface substitute its own address.
        /// @param destination The raw destination address.
        /// @param destination_type How the destination address is to be read.
        /// @param service The application layer service.
        /// @param payload The application payload.
        /// @param options The link layer flags.
        /// @return The number of octets written, or the reason the message could not be encoded.
        [[nodiscard]] constexpr std::expected<std::size_t, error> encode(const span_uint8_t dest, const cemi_message_code code,
                                                                         const individual_address source, const std::uint16_t destination,
                                                                         const knx::address_type destination_type, const apci service,
                                                                         const apdu_payload& payload,
                                                                         const l_data_options& options = {}) noexcept
        {
            if (!is_l_data(code))
                return std::unexpected(error::unsupported_message_code);
            if (payload.octets().size() > apdu_payload::max_octets)
                return std::unexpected(error::payload_too_large);
            if (options.hop_count > 0x07u)
                return std::unexpected(error::invalid_configuration);

            const auto data_length = payload.data_length();
            const auto size = encoded_size(0u, data_length);
            if (dest.size() < size)
                return std::unexpected(error::invalid_length);

            const auto raw_service = static_cast<std::uint16_t>(service);
            dest[0u] = static_cast<std::uint8_t>(code);
            dest[1u] = 0u;
            dest[2u] = make_control_field_1(options, data_length);
            dest[3u] = make_control_field_2(destination_type, options.hop_count);
            dest[4u] = static_cast<std::uint8_t>(source.value() >> 8u);
            dest[5u] = static_cast<std::uint8_t>(source.value() & 0xFFu);
            dest[6u] = static_cast<std::uint8_t>(destination >> 8u);
            dest[7u] = static_cast<std::uint8_t>(destination & 0xFFu);
            dest[8u] = data_length;
            dest[9u] = static_cast<std::uint8_t>(tpci_unnumbered_data | ((raw_service >> 8u) & 0x03u));
            dest[10u] = static_cast<std::uint8_t>(raw_service & 0xFFu);

            if (payload.compacted())
                dest[10u] = static_cast<std::uint8_t>(dest[10u] | payload.compact_value());
            else
            {
                const auto octets = payload.octets();
                for (std::size_t i {}; i < octets.size(); ++i)
                    dest[min_l_data_size + i] = octets[i];
            }

            return size;
        }

        /// @brief Encodes one cEMI L_Data message addressed to a group.
        /// @param dest The buffer to write into.
        /// @param code The message code; must be one of the L_Data codes.
        /// @param source The sending device.
        /// @param destination The destination group.
        /// @param service The application layer service.
        /// @param payload The application payload.
        /// @param options The link layer flags.
        /// @return The number of octets written, or the reason the message could not be encoded.
        [[nodiscard]] constexpr std::expected<std::size_t, error> encode(const span_uint8_t dest, const cemi_message_code code,
                                                                         const individual_address source, const group_address destination,
                                                                         const apci service, const apdu_payload& payload,
                                                                         const l_data_options& options = {}) noexcept
        {
            return encode(dest, code, source, destination.value(), knx::address_type::group, service, payload, options);
        }

        /// @brief Encodes one cEMI L_Data message addressed to a single device.
        /// @param dest The buffer to write into.
        /// @param code The message code; must be one of the L_Data codes.
        /// @param source The sending device.
        /// @param destination The destination device.
        /// @param service The application layer service.
        /// @param payload The application payload.
        /// @param options The link layer flags.
        /// @return The number of octets written, or the reason the message could not be encoded.
        [[nodiscard]] constexpr std::expected<std::size_t, error> encode(const span_uint8_t dest, const cemi_message_code code,
                                                                         const individual_address source,
                                                                         const individual_address destination, const apci service,
                                                                         const apdu_payload& payload,
                                                                         const l_data_options& options = {}) noexcept
        {
            return encode(dest, code, source, destination.value(), knx::address_type::individual, service, payload, options);
        }

        /// @brief Encodes an A_GroupValue_Write request.
        /// @param dest The buffer to write into.
        /// @param destination The destination group.
        /// @param payload The value to write.
        /// @param source The sending device; unset lets the interface substitute its own address.
        /// @param options The link layer flags.
        /// @return The number of octets written, or the reason the message could not be encoded.
        [[nodiscard]] constexpr std::expected<std::size_t, error> encode_group_value_write(const span_uint8_t dest,
                                                                                           const group_address destination,
                                                                                           const apdu_payload& payload,
                                                                                           const individual_address source = {},
                                                                                           const l_data_options& options = {}) noexcept
        {
            return encode(dest, cemi_message_code::l_data_req, source, destination, apci::group_value_write, payload, options);
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
            return encode(dest, cemi_message_code::l_data_req, source, destination, apci::group_value_read, apdu_payload {}, options);
        }

        /// @brief Encodes an A_GroupValue_Response.
        /// @param dest The buffer to write into.
        /// @param destination The destination group.
        /// @param payload The value to report.
        /// @param source The sending device; unset lets the interface substitute its own address.
        /// @param options The link layer flags.
        /// @return The number of octets written, or the reason the message could not be encoded.
        [[nodiscard]] constexpr std::expected<std::size_t, error> encode_group_value_response(const span_uint8_t dest,
                                                                                              const group_address destination,
                                                                                              const apdu_payload& payload,
                                                                                              const individual_address source = {},
                                                                                              const l_data_options& options = {}) noexcept
        {
            return encode(dest, cemi_message_code::l_data_req, source, destination, apci::group_value_response, payload, options);
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

            cemi_frame decoded {};
            decoded.message_code = code;
            decoded.additional_info_length = additional_info_length;
            decoded.control_field_1 = bytes[link_offset];
            decoded.control_field_2 = bytes[link_offset + 1u];
            decoded.source = individual_address {
                static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[link_offset + 2u]) << 8u) | bytes[link_offset + 3u])};
            decoded.destination =
                static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[link_offset + 4u]) << 8u) | bytes[link_offset + 5u]);
            decoded.data_length = data_length;
            decoded.transport_control = static_cast<std::uint8_t>(bytes[link_offset + 7u] & tpci_mask);
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
