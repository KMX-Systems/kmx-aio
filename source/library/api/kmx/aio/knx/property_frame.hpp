/// @file api/kmx/aio/knx/property_frame.hpp
/// @brief A decoded cEMI device management property service.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @reference KNX System Specifications, Volume 3/6/3 "EMI/IMI", cEMI device management.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/cemi_frame.hpp>

        #include <cstdint>
        #include <span>
    #endif

namespace kmx::aio::knx
{
    /// @brief One cEMI device management property service.
    /// @details The device management half of cEMI: the messages that read and write the interface object
    ///          properties of a device, carried by DEVICE_CONFIGURATION_REQUEST rather than by tunnelling.
    ///          Its shape has nothing in common with L_Data - no addresses, no APCI - which is why it is a
    ///          separate frame type rather than a variant of @ref kmx::aio::knx::cemi_frame.
    ///
    /// Wire layout, all fields big-endian:
    ///
    /// | Offset | Size | Field |
    /// | :--- | :--- | :--- |
    /// | 0 | 1 | message code |
    /// | 1 | 2 | interface object type |
    /// | 3 | 1 | object instance |
    /// | 4 | 1 | property id |
    /// | 5 | 2 | element count in the high four bits, start index in the low twelve |
    /// | 7 | n | the property data, absent from a read request |
    /// @reference KNX System Specifications, Volume 3/6/3 "EMI/IMI", cEMI device management.
    struct property_frame
    {
        /// @brief The message code.
        cemi_message_code message_code = cemi_message_code::m_prop_read_req;
        /// @brief The interface object type.
        std::uint16_t object_type {};
        /// @brief Which instance of that object type, counted from one.
        std::uint8_t object_instance {};
        /// @brief The property identifier.
        std::uint8_t property_id {};
        /// @brief How many elements the service names; zero in a confirmation reports an error.
        std::uint8_t element_count {};
        /// @brief The first element, counted from one; zero names the element count itself.
        std::uint16_t start_index {};
        /// @brief Offset of the property data within the buffer this frame was decoded from.
        std::uint16_t data_offset {};
        /// @brief Size of the property data.
        std::uint16_t data_size {};

        /// @brief Indicates whether a confirmation reports a failure.
        /// @details A confirmation with no elements carries a one-octet error code instead of data, which
        ///          is the only way a device management service reports that it refused the request.
        [[nodiscard]] constexpr bool failed() const noexcept
        {
            return ((message_code == cemi_message_code::m_prop_read_con) || (message_code == cemi_message_code::m_prop_write_con)) &&
                   (element_count == 0u);
        }

        /// @brief Returns the property data, mapped onto the buffer this frame was decoded from.
        /// @param bytes The very buffer it was decoded from.
        /// @return The data octets, empty when the message carries none.
        [[nodiscard]] constexpr cspan_uint8_t data(const cspan_uint8_t bytes) const noexcept
        {
            if ((data_size == 0u) || ((static_cast<std::size_t>(data_offset) + data_size) > bytes.size()))
                return {};

            return bytes.subspan(data_offset, data_size);
        }
    };
}
#endif // KMX_AIO_FEATURE_KNX
