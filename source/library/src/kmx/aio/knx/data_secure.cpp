/// @file src/kmx/aio/knx/data_secure.cpp
/// @brief The compiled body of the KNX Data Secure codec, and of the configuration built from a keyring.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/data_secure.hpp>
#ifndef PCH
    #include <kmx/aio/knx/cemi.hpp>
    #include <kmx/aio/knx/data_secure/detail/common.hpp>
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/group_address.hpp>
    #include <kmx/aio/knx/secure/detail/ccm.hpp>
    #include <kmx/aio/knx/secure/detail/crypto.hpp>
    #include <kmx/aio/knx/secure/secret_bytes.hpp>

    #include <algorithm>
    #include <array>
    #include <iterator>
#endif

namespace kmx::aio::knx::data_secure
{
    namespace sd = kmx::aio::knx::secure::detail;

    /// @brief Control field 2 bits that enter B0: the address type and the extended frame format.
    static constexpr std::uint8_t binding_control_mask = 0x8Fu;

    std::uint8_t encode_security_control(const security_control& value) noexcept
    {
        return static_cast<std::uint8_t>((value.tool_access ? 0x80u : 0x00u) | (static_cast<std::uint8_t>(value.algorithm) << 4u) |
                                         (value.system_broadcast ? 0x08u : 0x00u) | static_cast<std::uint8_t>(value.service));
    }

    security_control_result_t decode_security_control(const std::uint8_t octet) noexcept
    {
        const auto algorithm_bits = static_cast<std::uint8_t>((octet >> 4u) & 0x07u);
        const auto service_bits = static_cast<std::uint8_t>(octet & 0x07u);
        if (algorithm_bits > static_cast<std::uint8_t>(algorithm::authenticated_encryption))
            return detail::refuse(error::secure_unsupported);
        if ((service_bits != static_cast<std::uint8_t>(security_service::data)) &&
            (service_bits != static_cast<std::uint8_t>(security_service::sync_request)) &&
            (service_bits != static_cast<std::uint8_t>(security_service::sync_response)))
            return detail::refuse(error::secure_unsupported);
        return security_control {.tool_access = (octet & 0x80u) != 0u,
                                 .algorithm = static_cast<data_secure::algorithm>(algorithm_bits),
                                 .system_broadcast = (octet & 0x08u) != 0u,
                                 .service = static_cast<security_service>(service_bits)};
    }

    /// @brief Writes a sequence number as six big-endian octets.
    static void write_sequence(const span_uint8_t destination, const std::uint64_t sequence) noexcept
    {
        for (std::size_t index {}; index < sequence_size; ++index)
            destination[index] = static_cast<std::uint8_t>(sequence >> (8u * (sequence_size - 1u - index)));
    }

    std::uint64_t sequence_of(const cspan_uint8_t secured) noexcept
    {
        std::uint64_t sequence {};
        for (std::size_t index {}; index < sequence_size; ++index)
            sequence = (sequence << 8u) | secured[1u + index];
        return sequence;
    }

    /// @brief Writes the sequence number and both addresses, the first ten octets B0 and Ctr0 share.
    static void write_prefix(sd::block_t& block, const cspan_uint8_t sequence, const frame_binding& binding) noexcept
    {
        std::copy_n(sequence.begin(), sequence_size, block.begin());
        block[6u] = static_cast<std::uint8_t>(binding.source.value() >> 8u);
        block[7u] = static_cast<std::uint8_t>(binding.source.value() & 0xFFu);
        block[8u] = static_cast<std::uint8_t>(binding.destination >> 8u);
        block[9u] = static_cast<std::uint8_t>(binding.destination & 0xFFu);
    }

    /// @brief Builds B0: `sequence || source || destination || 00 || (address type | frame format) || (TPCI | 03) || F1 || 00 || length`.
    [[nodiscard]] static sd::block_t telegram_block_0(const cspan_uint8_t sequence, const frame_binding& binding,
                                                      const std::uint8_t payload_length) noexcept
    {
        sd::block_t block {};
        write_prefix(block, sequence, binding);
        block[11u] = static_cast<std::uint8_t>(binding.control_field_2 & binding_control_mask);
        block[12u] = static_cast<std::uint8_t>((binding.transport_control & cemi::tpci_mask) | detail::service_apci_high);
        block[13u] = detail::service_apci_low;
        block[15u] = payload_length;
        return block;
    }

    /// @brief Builds Ctr0: `sequence || source || destination || 00 00 00 00 01 00`.
    [[nodiscard]] static sd::block_t telegram_counter_0(const cspan_uint8_t sequence, const frame_binding& binding) noexcept
    {
        sd::block_t block {};
        write_prefix(block, sequence, binding);
        block[14u] = 0x01u;
        return block;
    }

    /// @brief What the MAC of one telegram covers, and the key it is computed under.
    struct telegram_mac_params
    {
        /// @brief The key of the destination.
        const secure::secret_key& key;
        /// @brief The security control field octet.
        std::uint8_t control_octet {};
        /// @brief The six sequence number octets.
        cspan_uint8_t sequence {};
        /// @brief The frame fields the MAC binds.
        frame_binding binding {};
        /// @brief How the telegram is protected.
        algorithm protection {algorithm::authenticated_encryption};
        /// @brief The plain APDU.
        cspan_uint8_t plain {};
    };

    /// @brief Computes the full CBC-MAC of a telegram, of which the first @ref mac_size octets are used.
    [[nodiscard]] static sd::mac_result_t telegram_mac(const telegram_mac_params& params) noexcept
    {
        const sd::cipher with {sd::evp_backend(), params.key};
        if (params.protection == algorithm::authenticated_encryption)
        {
            const std::array<std::uint8_t, 1u> associated {params.control_octet};
            return sd::cbc_mac(with, telegram_block_0(params.sequence, params.binding, static_cast<std::uint8_t>(params.plain.size())),
                               associated, params.plain);
        }

        // Authentication only: the APDU is associated data, the payload is empty, and B0 says so.
        std::array<std::uint8_t, 1u + max_plain_apdu> associated {};
        associated[0u] = params.control_octet;
        std::ranges::copy(params.plain, associated.begin() + 1);
        return sd::cbc_mac(with, telegram_block_0(params.sequence, params.binding, 0u), {associated.data(), 1u + params.plain.size()}, {});
    }

    expected_size_t seal_apdu(const span_uint8_t destination, const secure::secret_key& key, const apdu_fields& fields,
                              const cspan_uint8_t plain_apdu) noexcept
    {
        const auto size = plain_apdu.size() + secured_apdu_overhead;
        if ((plain_apdu.size() > max_plain_apdu) || (destination.size() < size))
            return detail::refuse(error::invalid_length);
        if (fields.sequence > max_sequence)
            return detail::refuse(error::invalid_configuration);
        destination[0u] = encode_security_control(fields.control);
        write_sequence(destination.subspan(1u, sequence_size), fields.sequence);
        const cspan_uint8_t sequence_octets {destination.data() + 1u, sequence_size};
        const auto apdu = destination.subspan(1u + sequence_size, plain_apdu.size());
        std::ranges::copy(plain_apdu, apdu.begin());
        const auto mac = telegram_mac({.key = key,
                                       .control_octet = destination[0u],
                                       .sequence = sequence_octets,
                                       .binding = fields.binding,
                                       .protection = fields.control.algorithm,
                                       .plain = apdu});
        if (!mac.has_value())
            return std::unexpected(mac.error());

        const auto mac_octets = destination.subspan(1u + sequence_size + plain_apdu.size(), mac_size);
        std::copy_n(mac->begin(), mac_size, mac_octets.begin());
        // One keystream from Ctr0: the four MAC octets first, then the APDU from the fifth keystream octet on.
        if (fields.control.algorithm == algorithm::authenticated_encryption)
            if (const auto encrypted =
                    sd::ctr({sd::evp_backend(), key}, telegram_counter_0(sequence_octets, fields.binding), mac_octets, apdu);
                !encrypted.has_value())
                return std::unexpected(encrypted.error());
        return size;
    }

    expected_size_t open_apdu(const span_uint8_t destination, const secure::secret_key& key, const frame_binding& binding,
                              const cspan_uint8_t secured) noexcept
    {
        if (secured.size() < secured_apdu_overhead)
            return detail::refuse(error::malformed_frame);
        const auto control = decode_security_control(secured[0u]);
        if (!control.has_value())
            return std::unexpected(control.error());
        const auto size = secured.size() - secured_apdu_overhead;
        if ((size > max_plain_apdu) || (destination.size() < size))
            return detail::refuse(error::invalid_length);

        const cspan_uint8_t sequence {secured.data() + 1u, sequence_size};
        const auto plain = destination.first(size);
        std::copy_n(secured.begin() + 1 + sequence_size, size, plain.begin());
        std::array<std::uint8_t, mac_size> received_mac {};
        std::copy_n(secured.end() - mac_size, mac_size, received_mac.begin());
        if (control->algorithm == algorithm::authenticated_encryption)
            if (const auto decrypted = sd::ctr({sd::evp_backend(), key}, telegram_counter_0(sequence, binding), received_mac, plain);
                !decrypted)
                return std::unexpected(decrypted.error());

        const auto expected_mac = telegram_mac({.key = key,
                                                .control_octet = secured[0u],
                                                .sequence = sequence,
                                                .binding = binding,
                                                .protection = control->algorithm,
                                                .plain = plain});
        if (expected_mac.has_value() && sd::constant_time_equal({expected_mac->data(), mac_size}, received_mac))
            return size;
        secure::detail::cleanse(plain);
        return expected_mac.has_value() ? detail::refuse(error::secure_authentication_failed) : std::unexpected(expected_mac.error());
    }

    /// @brief Indicates whether an interface receives a group.
    [[nodiscard]] static bool receives(const keyring::interface_entry& interface, const group_address address) noexcept
    {
        return std::ranges::any_of(interface.groups,
                                   [address](const keyring::group_senders& value) noexcept { return value.address == address; });
    }

    /// @brief Returns every sender the keyring names: those its interfaces list for their groups, with no sequence number
    ///        recorded, and its devices, with the sequence number ETS last recorded for each.
    [[nodiscard]] static std::vector<sender_sequence> senders_of(const keyring::document& value) noexcept(false)
    {
        std::vector<sender_sequence> senders {};
        const auto record = [&senders](const individual_address address, const std::uint64_t sequence)
        {
            const auto found = std::ranges::find(senders, address, &sender_sequence::address);
            if (found == senders.end())
                senders.push_back(sender_sequence {.address = address, .last_valid_sequence = sequence});
            else
                found->last_valid_sequence = std::max(found->last_valid_sequence, sequence);
        };
        for (const auto& interface: value.interfaces)
            for (const auto& group: interface.groups)
                for (const auto sender: group.senders)
                    record(sender, 0u);
        for (const auto& device: value.devices)
            record(device.address, device.sequence_number);
        return senders;
    }

    configuration_result_t configuration_for(const keyring::document& value, const individual_address local_address) noexcept(false)
    {
        configuration result {.local_address = local_address};
        const auto* const local = value.find_interface(local_address);
        for (const auto& key: value.group_keys)
            if ((local == nullptr) || receives(*local, key.address))
                result.group_keys.push_back(keyring::group_key {.address = key.address, .key = key.key.clone()});
        if (result.group_keys.empty())
            return detail::refuse(error::secure_key_missing);
        result.senders = senders_of(value);
        if (local != nullptr)
            std::ranges::copy_if(local->groups, std::back_inserter(result.allowed_senders),
                                 [](const keyring::group_senders& group) { return !group.senders.empty(); });
        if ((result.group_keys.size() > max_table_entries) || (result.senders.size() > max_table_entries))
            return detail::refuse(error::invalid_configuration);
        return result;
    }
}
