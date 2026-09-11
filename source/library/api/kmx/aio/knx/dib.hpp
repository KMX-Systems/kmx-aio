/// @file api/kmx/aio/knx/dib.hpp
/// @brief Description Information Blocks: what a KNXnet/IP server says about itself.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// A SEARCH_RESPONSE and a DESCRIPTION_RESPONSE are a HPAI followed by a run of description information
/// blocks, and every block shares a two-octet prologue: its own structure length, then its type. That
/// shape is what lets a reader walk blocks it does not understand, which is why an unrecognised type is
/// preserved here rather than rejected - a server may legitimately describe itself with more than a given
/// client models.
///
/// The blocks this header models by name are the ones a client acts on: what the device is, which service
/// families it serves and at which versions, how its IP is configured, and which KNX addresses it owns.
/// Everything else is kept as @ref kmx::aio::knx::dib::unknown_block so a round trip is lossless.
/// @reference KNX System Specifications, 03/08/02 "Core", description information block.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/ipv4.hpp>
        #include <kmx/aio/knx/dib/device_info.hpp>
        #include <kmx/aio/knx/dib/supported_service_families.hpp>
        #include <kmx/aio/knx/error.hpp>
        #include <kmx/aio/knx/individual_address.hpp>

        #include <cstdint>
        #include <expected>
        #include <span>
        #include <system_error>
        #include <variant>
        #include <vector>
    #endif

namespace kmx::aio::knx::dib
{
    /// @brief The description information block types KNXnet/IP defines.
    enum class block_type : std::uint8_t
    {
        /// @brief What the device is: medium, address, serial number, name.
        device_info = 0x01u,
        /// @brief Which service families the server serves, and at which versions.
        supported_service_families = 0x02u,
        /// @brief The IP configuration the device was told to use.
        ip_config = 0x03u,
        /// @brief The IP configuration the device is actually using.
        current_ip_config = 0x04u,
        /// @brief The individual addresses the device owns.
        knx_addresses = 0x05u,
        /// @brief Which service families the server serves only over a secure connection.
        secured_service_families = 0x06u,
        /// @brief How many tunnelling slots the server has, and their addresses.
        tunnelling_info = 0x07u,
        /// @brief The device descriptor and mask version.
        extended_device_info = 0x08u,
        /// @brief Manufacturer-specific data.
        manufacturer_data = 0xFEu,
    };

    /// @brief Indicates whether a code names a service family this build knows.
    [[nodiscard]] constexpr bool known_service_family(const std::uint8_t value) noexcept
    {
        switch (static_cast<service_family>(value))
        {
            case service_family::core:
            case service_family::device_management:
            case service_family::tunnelling:
            case service_family::routing:
            case service_family::remote_logging:
            case service_family::remote_configuration:
            case service_family::object_server:
            case service_family::security:
                return true;
        }

        return false;
    }

    /// @brief Names the service family a KNXnet/IP service type belongs to.
    /// @param service_type The service type identifier.
    /// @return The family, or nothing when the high octet names none.
    /// @details A service type is `family << 8 | index`, so the family is the high octet. This is what
    ///          lets a server decide whether it serves a datagram by consulting the families it advertises
    ///          rather than a second list that can drift from the first.
    [[nodiscard]] constexpr std::expected<service_family, error> family_of(const std::uint16_t service_type) noexcept
    {
        const auto family = static_cast<std::uint8_t>(service_type >> 8u);
        if (!known_service_family(family))
            return std::unexpected(error::unsupported_service);

        return static_cast<service_family>(family);
    }

    /// @brief Size of the structure length and type octets every block begins with.
    inline constexpr std::size_t block_header_size = 2u;

    /// @brief An IP_CONFIG block: the configuration the device was told to use.
    struct ip_config
    {
        /// @brief The configured IP address.
        ipv4::storage_t address {};
        /// @brief The configured subnet mask.
        ipv4::storage_t subnet_mask {};
        /// @brief The configured default gateway.
        ipv4::storage_t default_gateway {};
        /// @brief Which assignment methods the device supports.
        std::uint8_t capabilities {};
        /// @brief Which assignment method is configured.
        std::uint8_t assignment_method {};
    };

    /// @brief An IP_CUR_CONFIG block: the configuration the device is actually using.
    struct current_ip_config
    {
        /// @brief The address the device is actually using.
        ipv4::storage_t address {};
        /// @brief The subnet mask it is actually using.
        ipv4::storage_t subnet_mask {};
        /// @brief The default gateway it is actually using.
        ipv4::storage_t default_gateway {};
        /// @brief The DHCP server it obtained its address from; all-zero when it did not.
        ipv4::storage_t dhcp_server {};
        /// @brief Which assignment method actually produced this configuration.
        std::uint8_t assignment_method {};
    };

    /// @brief A KNX_ADDRESSES block.
    struct addresses
    {
        /// @brief The device's own address.
        individual_address device {};
        /// @brief Any further addresses it owns, one per tunnelling slot.
        std::vector<individual_address> additional {};
    };

    /// @brief An MFR_DATA block.
    struct manufacturer_data
    {
        /// @brief The KNX manufacturer identifier the data belongs to.
        std::uint16_t manufacturer_id {};
        /// @brief The manufacturer-specific octets, uninterpreted.
        byte_buffer_t data {};
    };

    /// @brief A block whose type this build does not model, kept verbatim.
    /// @details Preserved rather than rejected so that decoding and re-encoding a server's description is
    ///          lossless even when it describes itself with blocks newer than this code.
    struct unknown_block
    {
        /// @brief The block type code, as it appeared on the wire.
        std::uint8_t type_code {};
        /// @brief The block's octets after the length and type.
        byte_buffer_t data {};
    };

    /// @brief One description information block.
    using block =
        std::variant<device_info, supported_service_families, ip_config, current_ip_config, addresses, manufacturer_data, unknown_block>;

    /// @brief Every block of a description, or the error explaining why it could not be read.
    using block_list_result_t = std::expected<std::vector<block>, std::error_code>;
    /// @brief One block, or the error explaining why it could not be read.
    using block_result_t = std::expected<block, std::error_code>;

    /// @brief Returns the number of octets a block occupies once encoded.
    /// @param value The block to measure.
    [[nodiscard]] std::size_t encoded_size(const block& value) noexcept;
    /// @brief Returns the number of octets a run of blocks occupies once encoded.
    /// @param values The blocks to measure.
    /// @note Call this to size the destination before @ref encode_all; blocks are variable-length.
    [[nodiscard]] std::size_t encoded_size(std::span<const block> values) noexcept;

    /// @brief Encodes one block.
    /// @param destination The buffer to write into.
    /// @param value The block to encode.
    /// @return The number of octets written, or why it could not be encoded.
    [[nodiscard]] std::expected<std::size_t, std::error_code> encode(span_uint8_t destination, const block& value) noexcept;

    /// @brief Encodes a run of blocks, one after another.
    /// @param destination The buffer to write into.
    /// @param values The blocks to encode, in order.
    /// @return The number of octets written, or why they could not be encoded.
    [[nodiscard]] std::expected<std::size_t, std::error_code> encode_all(span_uint8_t destination, std::span<const block> values) noexcept;

    /// @brief Decodes the first block of a run.
    /// @param bytes The octets, starting at the block's structure length.
    /// @return The block, or why it could not be read.
    [[nodiscard]] block_result_t decode(cspan_uint8_t bytes) noexcept;

    /// @brief Decodes every block of a description.
    /// @param bytes The whole run of blocks, as a search or description response carries it.
    /// @return The blocks in order, or why the run could not be read.
    /// @note A block type this build does not model becomes an @ref unknown_block rather than an error, so
    ///       a description is never rejected merely for being newer than this code.
    [[nodiscard]] block_list_result_t decode_all(cspan_uint8_t bytes) noexcept;

    /// @brief Indicates whether a run of octets is a well-formed sequence of blocks.
    /// @param bytes The octets to check.
    /// @return `true` when the run is well formed.
    /// @details Each block's structure length must be at least two and must not run past the end, and the
    ///          lengths together must account for the run exactly.
    /// @note Checks framing only, not that any given block's contents make sense; that is what
    ///       @ref decode_all is for.
    [[nodiscard]] bool valid_blocks(cspan_uint8_t bytes) noexcept;

    /// @brief Finds the supported service families block of a description, if it has one.
    /// @param values The decoded blocks.
    /// @param secured Whether to look for the secured variant.
    /// @return A pointer to the block, or null when the description carries none.
    [[nodiscard]] const supported_service_families* find_service_families(std::span<const block> values, bool secured = false) noexcept;

    /// @brief Finds the device information block of a description, if it has one.
    /// @param values The decoded blocks.
    /// @return A pointer to the block, or null when the description carries none.
    /// @warning The pointer is into @p values and is invalidated by anything that modifies it.
    [[nodiscard]] const device_info* find_device_info(std::span<const block> values) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
