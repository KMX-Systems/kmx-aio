/// @file src/kmx/aio/knx/dib.cpp
/// @brief KNXnet/IP Description Information Block encoding, decoding and validation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/dib.hpp>
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/dib/device_info.hpp>
    #include <kmx/aio/knx/dib/supported_service_families.hpp>
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/individual_address.hpp>

    #include <algorithm>
    #include <array>
    #include <cstdint>
    #include <expected>
    #include <optional>
    #include <span>
    #include <system_error>
    #include <variant>
    #include <vector>
#endif

namespace kmx::aio::knx::dib
{
    namespace
    {
        /// @brief Encoded size of a DEVICE_INFO block, which is fixed.
        constexpr std::size_t device_info_size = 54u;
        /// @brief Encoded size of an IP_CONFIG block, which is fixed.
        constexpr std::size_t ip_config_size = 16u;
        /// @brief Encoded size of an IP_CUR_CONFIG block, which is fixed.
        constexpr std::size_t current_ip_config_size = 20u;
        /// @brief Smallest MFR_DATA block: the prologue and the manufacturer code.
        constexpr std::size_t manufacturer_data_min_size = 4u;
        /// @brief Largest block a one-octet structure length can describe.
        constexpr std::size_t max_block_size = 0xFFu;

        [[nodiscard]] constexpr std::uint16_t read_u16(const cspan_uint8_t bytes, const std::size_t offset) noexcept
        {
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8u) | bytes[offset + 1u]);
        }

        constexpr void write_u16(const span_uint8_t bytes, const std::size_t offset, const std::uint16_t value) noexcept
        {
            bytes[offset] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
            bytes[offset + 1u] = static_cast<std::uint8_t>(value & 0xFFu);
        }

        template <std::size_t N>
        constexpr void read_octets(const cspan_uint8_t bytes, const std::size_t offset, std::array<std::uint8_t, N>& value) noexcept
        {
            std::copy_n(bytes.begin() + offset, N, value.begin());
        }

        template <std::size_t N>
        constexpr void write_octets(const span_uint8_t bytes, const std::size_t offset, const std::array<std::uint8_t, N>& value) noexcept
        {
            std::copy_n(value.begin(), N, bytes.begin() + offset);
        }

        /// @brief Writes the structure length and type every block begins with.
        constexpr void write_header(const span_uint8_t destination, const std::size_t size, const block_type type) noexcept
        {
            destination[0u] = static_cast<std::uint8_t>(size);
            destination[1u] = static_cast<std::uint8_t>(type);
        }

        /// @brief The encoded size of one block, resolved per alternative.
        /// @details One overload set answers both @ref encoded_size and the bound @ref encode writes
        ///          within, so the length a block reports and the length it occupies cannot drift apart.
        [[nodiscard]] constexpr std::size_t block_size(const device_info&) noexcept
        {
            return device_info_size;
        }

        [[nodiscard]] inline std::size_t block_size(const supported_service_families& value) noexcept
        {
            return block_header_size + (2u * value.families.size());
        }

        [[nodiscard]] constexpr std::size_t block_size(const ip_config&) noexcept
        {
            return ip_config_size;
        }

        [[nodiscard]] constexpr std::size_t block_size(const current_ip_config&) noexcept
        {
            return current_ip_config_size;
        }

        [[nodiscard]] inline std::size_t block_size(const addresses& value) noexcept
        {
            return block_header_size + 2u + (2u * value.additional.size());
        }

        [[nodiscard]] inline std::size_t block_size(const manufacturer_data& value) noexcept
        {
            return manufacturer_data_min_size + value.data.size();
        }

        [[nodiscard]] inline std::size_t block_size(const unknown_block& value) noexcept
        {
            return block_header_size + value.data.size();
        }
    }

    std::size_t encoded_size(const block& value) noexcept
    {
        return std::visit([](const auto& held) noexcept { return block_size(held); }, value);
    }

    std::size_t encoded_size(const std::span<const block> values) noexcept
    {
        std::size_t total {};
        for (const auto& value: values)
            total += encoded_size(value);
        return total;
    }

    /// @brief Writes a DEVICE_INFO block.
    /// @param destination The octets to write into; the caller has already bounds-checked them.
    /// @param size The block's encoded size, as @ref block_size reported it.
    /// @param value The block to write.
    static void write_block(const span_uint8_t destination, const std::size_t size, const device_info& value) noexcept
    {
        write_header(destination, size, block_type::device_info);
        destination[2u] = value.knx_medium;
        destination[3u] = value.device_status;
        write_u16(destination, 4u, value.address.value());
        write_u16(destination, 6u, value.project_installation_id);
        write_octets(destination, 8u, value.serial_number);
        write_octets(destination, 14u, value.multicast_address);
        write_octets(destination, 18u, value.mac_address);
        std::transform(value.friendly_name.begin(), value.friendly_name.end(), destination.begin() + 24u,
                       [](const char octet) noexcept { return static_cast<std::uint8_t>(octet); });
    }

    /// @copydoc write_block
    static void write_block(const span_uint8_t destination, const std::size_t size, const supported_service_families& value) noexcept
    {
        write_header(destination, size, value.secured ? block_type::secured_service_families : block_type::supported_service_families);
        std::size_t offset = block_header_size;
        for (const auto& entry: value.families)
        {
            destination[offset] = static_cast<std::uint8_t>(entry.family);
            destination[offset + 1u] = entry.version;
            offset += 2u;
        }
    }

    /// @copydoc write_block
    static void write_block(const span_uint8_t destination, const std::size_t size, const ip_config& value) noexcept
    {
        write_header(destination, size, block_type::ip_config);
        write_octets(destination, 2u, value.address);
        write_octets(destination, 6u, value.subnet_mask);
        write_octets(destination, 10u, value.default_gateway);
        destination[14u] = value.capabilities;
        destination[15u] = value.assignment_method;
    }

    /// @copydoc write_block
    static void write_block(const span_uint8_t destination, const std::size_t size, const current_ip_config& value) noexcept
    {
        write_header(destination, size, block_type::current_ip_config);
        write_octets(destination, 2u, value.address);
        write_octets(destination, 6u, value.subnet_mask);
        write_octets(destination, 10u, value.default_gateway);
        write_octets(destination, 14u, value.dhcp_server);
        destination[18u] = value.assignment_method;
        destination[19u] = 0u; // reserved
    }

    /// @copydoc write_block
    static void write_block(const span_uint8_t destination, const std::size_t size, const addresses& value) noexcept
    {
        write_header(destination, size, block_type::knx_addresses);
        write_u16(destination, 2u, value.device.value());
        std::size_t offset = 4u;
        for (const auto address: value.additional)
        {
            write_u16(destination, offset, address.value());
            offset += 2u;
        }
    }

    /// @copydoc write_block
    static void write_block(const span_uint8_t destination, const std::size_t size, const manufacturer_data& value) noexcept
    {
        write_header(destination, size, block_type::manufacturer_data);
        write_u16(destination, 2u, value.manufacturer_id);
        std::copy_n(value.data.begin(), value.data.size(), destination.begin() + manufacturer_data_min_size);
    }

    /// @copydoc write_block
    /// @note The type code is written verbatim: an unknown block is preserved exactly as it arrived.
    static void write_block(const span_uint8_t destination, const std::size_t size, const unknown_block& value) noexcept
    {
        destination[0u] = static_cast<std::uint8_t>(size);
        destination[1u] = value.type_code;
        std::copy_n(value.data.begin(), value.data.size(), destination.begin() + block_header_size);
    }

    /// @brief Measures one block, checks it fits, and writes it.
    /// @tparam T The alternative the block holds.
    /// @param destination The octets to write into.
    /// @param held The block to encode.
    /// @return How many octets were written, or why the block could not be encoded.
    template <typename T>
    [[nodiscard]] static std::expected<std::size_t, std::error_code> encode_block(const span_uint8_t destination, const T& held) noexcept
    {
        const auto size = block_size(held);
        // The structure length is one octet, so a block that cannot describe its own size is not encodable
        // at all - a truncated length would name a shorter block and hand the reader the rest as garbage.
        if (size > max_block_size)
            return std::unexpected(make_error_code(error::invalid_length));
        if (destination.size() < size)
            return std::unexpected(make_error_code(error::invalid_length));

        write_block(destination, size, held);
        return size;
    }

    std::expected<std::size_t, std::error_code> encode(const span_uint8_t destination, const block& value) noexcept
    {
        // One dispatch over the variant: the size that bounds the write and the write itself come out of
        // the same visit, and the overload set behind write_block is what selects the layout.
        return std::visit([destination](const auto& held) noexcept { return encode_block(destination, held); }, value);
    }

    std::expected<std::size_t, std::error_code> encode_all(const span_uint8_t destination, const std::span<const block> values) noexcept
    {
        std::size_t offset {};
        for (const auto& value: values)
        {
            const auto written = encode(destination.subspan(offset), value);
            if (!written.has_value())
                return std::unexpected(written.error());
            offset += *written;
        }

        return offset;
    }

    /// @brief Reads a DEVICE_INFO block, whose size is fixed.
    /// @param body The block's octets, prologue included.
    /// @return The decoded block, or why the octets are not one.
    [[nodiscard]] static block_result_t decode_device_info(const cspan_uint8_t body) noexcept
    {
        if (body.size() != device_info_size)
            return std::unexpected(make_error_code(error::malformed_frame));

        device_info decoded {};
        decoded.knx_medium = body[2u];
        decoded.device_status = body[3u];
        decoded.address = individual_address {read_u16(body, 4u)};
        decoded.project_installation_id = read_u16(body, 6u);
        read_octets(body, 8u, decoded.serial_number);
        read_octets(body, 14u, decoded.multicast_address);
        read_octets(body, 18u, decoded.mac_address);
        std::transform(body.begin() + 24u, body.begin() + 24u + friendly_name_size, decoded.friendly_name.begin(),
                       [](const std::uint8_t value) noexcept { return static_cast<char>(value); });
        return block {decoded};
    }

    /// @brief Reads a list of supported or secured service families.
    /// @param body The block's octets, prologue included.
    /// @param secured Whether the block is the secured variant.
    /// @return The decoded block, or why the octets are not one.
    [[nodiscard]] static block_result_t decode_service_families(const cspan_uint8_t body, const bool secured) noexcept
    {
        // Two octets per entry, so an odd body is not a list of them.
        if (((body.size() - block_header_size) % 2u) != 0u)
            return std::unexpected(make_error_code(error::malformed_frame));

        for (std::size_t offset = block_header_size; offset < body.size(); offset += 2u)
            if (!known_service_family(body[offset]))
                return block {unknown_block {body[1u], byte_buffer_t {body.begin() + block_header_size, body.end()}}};

        supported_service_families decoded {};
        decoded.secured = secured;
        decoded.families.reserve((body.size() - block_header_size) / 2u);
        for (std::size_t offset = block_header_size; offset < body.size(); offset += 2u)
            decoded.families.push_back(service_family_entry {static_cast<service_family>(body[offset]), body[offset + 1u]});
        return block {decoded};
    }

    /// @brief Reads an IP_CONFIG block, whose size is fixed.
    [[nodiscard]] static block_result_t decode_ip_config(const cspan_uint8_t body) noexcept
    {
        if (body.size() != ip_config_size)
            return std::unexpected(make_error_code(error::malformed_frame));

        ip_config decoded {};
        read_octets(body, 2u, decoded.address);
        read_octets(body, 6u, decoded.subnet_mask);
        read_octets(body, 10u, decoded.default_gateway);
        decoded.capabilities = body[14u];
        decoded.assignment_method = body[15u];
        return block {decoded};
    }

    /// @brief Reads an IP_CUR_CONFIG block, whose size is fixed.
    [[nodiscard]] static block_result_t decode_current_ip_config(const cspan_uint8_t body) noexcept
    {
        if (body.size() != current_ip_config_size)
            return std::unexpected(make_error_code(error::malformed_frame));

        current_ip_config decoded {};
        read_octets(body, 2u, decoded.address);
        read_octets(body, 6u, decoded.subnet_mask);
        read_octets(body, 10u, decoded.default_gateway);
        read_octets(body, 14u, decoded.dhcp_server);
        decoded.assignment_method = body[18u];
        return block {decoded};
    }

    /// @brief Reads a KNX_ADDRESSES block: the device's own address, then any additional ones.
    [[nodiscard]] static block_result_t decode_addresses(const cspan_uint8_t body) noexcept
    {
        if ((body.size() < 4u) || ((body.size() % 2u) != 0u))
            return std::unexpected(make_error_code(error::malformed_frame));

        addresses decoded {};
        decoded.device = individual_address {read_u16(body, 2u)};
        decoded.additional.reserve((body.size() - 4u) / 2u);
        for (std::size_t offset = 4u; offset < body.size(); offset += 2u)
            decoded.additional.push_back(individual_address {read_u16(body, offset)});
        return block {decoded};
    }

    /// @brief Reads a MFR_DATA block: the manufacturer code, then whatever that manufacturer put behind it.
    [[nodiscard]] static block_result_t decode_manufacturer_data(const cspan_uint8_t body) noexcept
    {
        if (body.size() < manufacturer_data_min_size)
            return std::unexpected(make_error_code(error::malformed_frame));

        manufacturer_data decoded {};
        decoded.manufacturer_id = read_u16(body, 2u);
        decoded.data.assign(body.begin() + manufacturer_data_min_size, body.end());
        return block {decoded};
    }

    /// @brief Keeps a block this build does not model as the octets it arrived as.
    /// @param body The block's octets, prologue included.
    /// @param type The type code to preserve.
    /// @return The block, which a later encode reproduces exactly.
    [[nodiscard]] static block decode_unknown(const cspan_uint8_t body, const std::uint8_t type) noexcept
    {
        unknown_block decoded {};
        decoded.type_code = type;
        decoded.data.assign(body.begin() + block_header_size, body.end());
        return block {decoded};
    }

    /// @brief Reads the prologue every block shares and returns just that block's octets.
    /// @param bytes The run of blocks, positioned at one block's first octet.
    /// @return The block's own octets, or nothing when the prologue does not describe one.
    /// @details A block shorter than its own prologue, or longer than what is left, would never advance a
    ///          reader walking the run.
    [[nodiscard]] static std::optional<cspan_uint8_t> block_body(const cspan_uint8_t bytes) noexcept
    {
        if (bytes.size() < block_header_size)
            return {};

        const std::size_t size = bytes[0u];
        if ((size < block_header_size) || (size > bytes.size()))
            return {};
        return bytes.first(size);
    }

    block_result_t decode(const cspan_uint8_t bytes) noexcept
    {
        const auto body = block_body(bytes);
        if (!body.has_value())
            return std::unexpected(make_error_code(error::malformed_frame));

        const auto type = bytes[1u];
        switch (static_cast<block_type>(type))
        {
            case block_type::device_info:
                return decode_device_info(*body);
            case block_type::supported_service_families:
                return decode_service_families(*body, false);
            case block_type::secured_service_families:
                return decode_service_families(*body, true);
            case block_type::ip_config:
                return decode_ip_config(*body);
            case block_type::current_ip_config:
                return decode_current_ip_config(*body);
            case block_type::knx_addresses:
                return decode_addresses(*body);
            case block_type::manufacturer_data:
                return decode_manufacturer_data(*body);
            case block_type::tunnelling_info:
            case block_type::extended_device_info:
                break; // named so the switch stays exhaustive; modelled only as their octets
        }

        return decode_unknown(*body, type);
    }

    block_list_result_t decode_all(const cspan_uint8_t bytes) noexcept
    {
        std::size_t block_count {};
        std::size_t scan_offset {};
        while ((scan_offset + block_header_size) <= bytes.size())
        {
            const std::size_t size = bytes[scan_offset];
            if ((size < block_header_size) || ((scan_offset + size) > bytes.size()))
                break;
            ++block_count;
            scan_offset += size;
        }

        std::vector<block> decoded {};
        decoded.reserve(block_count);
        std::size_t offset {};
        while (offset < bytes.size())
        {
            auto value = decode(bytes.subspan(offset));
            if (!value.has_value())
                return std::unexpected(value.error());

            offset += bytes[offset];
            decoded.push_back(std::move(*value));
        }

        return decoded;
    }

    bool valid_blocks(const cspan_uint8_t bytes) noexcept
    {
        std::size_t offset {};
        while (offset < bytes.size())
        {
            const auto remaining = bytes.size() - offset;
            const std::size_t size = bytes[offset];
            if ((size < block_header_size) || (size > remaining))
                return false;
            offset += size;
        }

        return offset == bytes.size();
    }

    const supported_service_families* find_service_families(const std::span<const block> values, const bool secured) noexcept
    {
        for (const auto& value: values)
            if (const auto* held = std::get_if<supported_service_families>(&value); (held != nullptr) && (held->secured == secured))
                return held;
        return nullptr;
    }

    const device_info* find_device_info(const std::span<const block> values) noexcept
    {
        for (const auto& value: values)
            if (const auto* held = std::get_if<device_info>(&value))
                return held;
        return nullptr;
    }
}
