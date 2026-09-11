/// @file api/kmx/aio/knx/cemi_frame.hpp
/// @brief A decoded cEMI L_Data message, and the message codes, priorities, address types and APCI values it names.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// Decoding never copies the payload: a decoded frame stores the payload's offset and size, and
/// @ref kmx::aio::knx::cemi_frame::payload maps that back onto the buffer it was decoded from.
/// @reference KNX System Specifications, Volume 3/6/3 "EMI/IMI", cEMI L_Data.
/// @reference KNX System Specifications, Volume 3/3/7 "Application Layer", APCI codes.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/group_address.hpp>
        #include <kmx/aio/knx/individual_address.hpp>

        #include <cstdint>
        #include <span>
    #endif

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
        /// @brief A_SecureService — a KNX Data Secure APDU; see `kmx/aio/knx/data_secure.hpp`.
        secure_service = 0x3F1u,
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
}
#endif // KMX_AIO_FEATURE_KNX
