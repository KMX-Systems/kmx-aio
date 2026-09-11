/// @file kmx/aio/knx/data_secure.cpp
/// @brief The compiled body of KNX Data Secure group communication.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/data_secure.hpp>

#include <kmx/aio/knx/cemi.hpp>
#include <kmx/aio/knx/error.hpp>
#include <kmx/aio/knx/secure/detail/ccm.hpp>
#include <kmx/aio/knx/secure/detail/crypto.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <utility>

namespace kmx::aio::knx::data_secure
{
    namespace sd = kmx::aio::knx::secure::detail;

    /// @brief The second APCI octet of A_SecureService.
    static constexpr std::uint8_t secure_service_low = 0xF1u;
    /// @brief The two APCI bits of A_SecureService in the first APDU octet.
    static constexpr std::uint8_t secure_service_high = 0x03u;
    /// @brief Control field 2 bits that enter B0: the address type and the extended frame format.
    static constexpr std::uint8_t binding_control_mask = 0x8Fu;
    /// @brief Where the first APDU octet sits, past the link header.
    static constexpr std::size_t apdu_offset = cemi::link_header_size;

    [[nodiscard]] static std::unexpected<std::error_code> refuse(const error reason) noexcept
    {
        return std::unexpected(make_error_code(reason));
    }

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
            return refuse(error::secure_unsupported);
        if ((service_bits != static_cast<std::uint8_t>(security_service::data)) &&
            (service_bits != static_cast<std::uint8_t>(security_service::sync_request)) &&
            (service_bits != static_cast<std::uint8_t>(security_service::sync_response)))
            return refuse(error::secure_unsupported);
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
    [[nodiscard]] static sd::block_t data_block_0(const cspan_uint8_t sequence, const frame_binding& binding,
                                                  const std::uint8_t payload_length) noexcept
    {
        sd::block_t block {};
        write_prefix(block, sequence, binding);
        block[11u] = static_cast<std::uint8_t>(binding.control_field_2 & binding_control_mask);
        block[12u] = static_cast<std::uint8_t>((binding.transport_control & cemi::tpci_mask) | secure_service_high);
        block[13u] = secure_service_low;
        block[15u] = payload_length;
        return block;
    }

    /// @brief Builds Ctr0: `sequence || source || destination || 00 00 00 00 01 00`.
    [[nodiscard]] static sd::block_t data_counter_0(const cspan_uint8_t sequence, const frame_binding& binding) noexcept
    {
        sd::block_t block {};
        write_prefix(block, sequence, binding);
        block[14u] = 0x01u;
        return block;
    }

    /// @brief Computes the full CBC-MAC of a telegram, of which the first @ref mac_size octets are used.
    [[nodiscard]] static sd::mac_result_t data_mac(const secure::secret_key& key, const std::uint8_t control_octet,
                                                   const cspan_uint8_t sequence, const frame_binding& binding, const algorithm protection,
                                                   const cspan_uint8_t plain) noexcept
    {
        const auto& backend = sd::evp_backend();
        if (protection == algorithm::authenticated_encryption)
        {
            const std::array<std::uint8_t, 1u> associated {control_octet};
            return sd::cbc_mac(backend, key, data_block_0(sequence, binding, static_cast<std::uint8_t>(plain.size())), associated, plain);
        }
        // Authentication only: the APDU is associated data, the payload is empty, and B0 says so.
        std::array<std::uint8_t, 1u + max_plain_apdu> associated {};
        associated[0u] = control_octet;
        std::ranges::copy(plain, associated.begin() + 1);
        return sd::cbc_mac(backend, key, data_block_0(sequence, binding, 0u), {associated.data(), 1u + plain.size()}, {});
    }

    expected_size_t seal_apdu(const span_uint8_t destination, const secure::secret_key& key, const security_control& control,
                              const std::uint64_t sequence, const frame_binding& binding, const cspan_uint8_t plain_apdu) noexcept
    {
        const auto size = plain_apdu.size() + secured_apdu_overhead;
        if ((plain_apdu.size() > max_plain_apdu) || (destination.size() < size))
            return refuse(error::invalid_length);
        if (sequence > max_sequence)
            return refuse(error::invalid_configuration);
        destination[0u] = encode_security_control(control);
        write_sequence(destination.subspan(1u, sequence_size), sequence);
        const cspan_uint8_t sequence_octets {destination.data() + 1u, sequence_size};
        const auto apdu = destination.subspan(1u + sequence_size, plain_apdu.size());
        std::ranges::copy(plain_apdu, apdu.begin());
        const auto mac = data_mac(key, destination[0u], sequence_octets, binding, control.algorithm, apdu);
        if (!mac.has_value())
            return std::unexpected(mac.error());

        const auto mac_octets = destination.subspan(1u + sequence_size + plain_apdu.size(), mac_size);
        std::copy_n(mac->begin(), mac_size, mac_octets.begin());
        // One keystream from Ctr0: the four MAC octets first, then the APDU from the fifth keystream octet on.
        if (control.algorithm == algorithm::authenticated_encryption)
            if (const auto encrypted = sd::ctr(sd::evp_backend(), key, data_counter_0(sequence_octets, binding), mac_octets, apdu);
                !encrypted.has_value())
                return std::unexpected(encrypted.error());
        return size;
    }

    expected_size_t open_apdu(const span_uint8_t destination, const secure::secret_key& key, const frame_binding& binding,
                              const cspan_uint8_t secured) noexcept
    {
        if (secured.size() < secured_apdu_overhead)
            return refuse(error::malformed_frame);
        const auto control = decode_security_control(secured[0u]);
        if (!control.has_value())
            return std::unexpected(control.error());
        const auto size = secured.size() - secured_apdu_overhead;
        if ((size > max_plain_apdu) || (destination.size() < size))
            return refuse(error::invalid_length);

        const cspan_uint8_t sequence {secured.data() + 1u, sequence_size};
        const auto plain = destination.first(size);
        std::copy_n(secured.begin() + 1 + sequence_size, size, plain.begin());
        std::array<std::uint8_t, mac_size> received_mac {};
        std::copy_n(secured.end() - mac_size, mac_size, received_mac.begin());
        if (control->algorithm == algorithm::authenticated_encryption)
            if (const auto decrypted = sd::ctr(sd::evp_backend(), key, data_counter_0(sequence, binding), received_mac, plain); !decrypted)
                return std::unexpected(decrypted.error());

        const auto expected_mac = data_mac(key, secured[0u], sequence, binding, control->algorithm, plain);
        if (expected_mac.has_value() && sd::constant_time_equal({expected_mac->data(), mac_size}, received_mac))
            return size;
        secure::detail::cleanse(plain);
        return expected_mac.has_value() ? refuse(error::secure_authentication_failed) : std::unexpected(expected_mac.error());
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
        {
            if ((local == nullptr) || receives(*local, key.address))
                result.group_keys.push_back(keyring::group_key {.address = key.address, .key = key.key.clone()});
        }
        if (result.group_keys.empty())
            return refuse(error::secure_key_missing);
        result.senders = senders_of(value);
        if (local != nullptr)
            std::ranges::copy_if(local->groups, std::back_inserter(result.allowed_senders),
                                 [](const keyring::group_senders& group) { return !group.senders.empty(); });
        if ((result.group_keys.size() > max_table_entries) || (result.senders.size() > max_table_entries))
            return refuse(error::invalid_configuration);
        return result;
    }

    struct context::frame_view
    {
        cemi_message_code code {cemi_message_code::l_data_ind};
        /// @brief Where the link header starts, past any additional information.
        std::size_t link_offset {};
        std::uint8_t control_field_1 {};
        frame_binding binding {};
        std::uint8_t data_length {};
        bool group {};
        bool secured {};
    };

    context::context(configuration value, sequence_store* const store, const secure::wall_clock_ms_function wall_clock) noexcept:
        configuration_(std::move(value)),
        store_(store),
        wall_clock_(wall_clock)
    {
        valid_ = (configuration_.group_keys.size() <= max_table_entries) && (configuration_.senders.size() <= max_table_entries);
    }

    std::optional<context::frame_view> context::view(const cspan_uint8_t cemi) noexcept
    {
        if ((cemi.size() < cemi::prologue_size) || !cemi::is_l_data(static_cast<cemi_message_code>(cemi[0u])))
            return std::nullopt;
        const std::size_t link = cemi::prologue_size + cemi[1u];
        if (((link + apdu_offset + 1u) > cemi.size()) || (cemi::encoded_size(cemi[1u], cemi[link + 6u]) != cemi.size()))
            return std::nullopt;

        frame_view value {.code = static_cast<cemi_message_code>(cemi[0u]), .link_offset = link, .control_field_1 = cemi[link]};
        value.binding = frame_binding {.source = individual_address {static_cast<std::uint16_t>((cemi[link + 2u] << 8u) | cemi[link + 3u])},
                                       .destination = static_cast<std::uint16_t>((cemi[link + 4u] << 8u) | cemi[link + 5u]),
                                       .control_field_2 = cemi[link + 1u],
                                       .transport_control = static_cast<std::uint8_t>(cemi[link + apdu_offset] & cemi::tpci_mask)};
        value.data_length = cemi[link + 6u];
        value.group = (value.binding.control_field_2 & cemi_frame::address_type_mask) != 0u;
        value.secured = (value.data_length >= 2u) && ((cemi[link + apdu_offset] & secure_service_high) == secure_service_high) &&
                        (cemi[link + apdu_offset + 1u] == secure_service_low);
        return value;
    }

    const secure::secret_key* context::key_for(const std::uint16_t group) const noexcept
    {
        const auto found = std::ranges::find_if(configuration_.group_keys, [group](const keyring::group_key& value) noexcept
                                                { return value.address.value() == group; });
        return (found == configuration_.group_keys.end()) ? nullptr : &found->key;
    }

    bool context::sender_allowed(const individual_address source, const std::uint16_t group) const noexcept
    {
        const auto listed = std::ranges::find_if(configuration_.allowed_senders, [group](const keyring::group_senders& value) noexcept
                                                 { return value.address.value() == group; });
        return (listed == configuration_.allowed_senders.end()) || (std::ranges::find(listed->senders, source) != listed->senders.end());
    }

    bool context::secured_group(const group_address address) const noexcept
    {
        const std::lock_guard lock {mutex_};
        return key_for(address.value()) != nullptr;
    }

    secure::statistics context::counters() const noexcept
    {
        const std::lock_guard lock {mutex_};
        return counters_;
    }

    std::uint64_t context::starting_sequence() const noexcept
    {
        const auto now =
            (wall_clock_ != nullptr) ?
                wall_clock_() :
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        // Milliseconds since 2018-01-05, as ETS and xknx count; never zero, which no receiver accepts.
        return std::max<std::uint64_t>((now > sequence_epoch_ms) ? (now - sequence_epoch_ms) : 0u, 1u);
    }

    std::expected<std::uint64_t, std::error_code> context::next_sequence() noexcept
    {
        if (!next_sequence_.has_value() && (store_ == nullptr))
            next_sequence_ = starting_sequence();
        if (!next_sequence_.has_value())
        {
            const auto loaded = store_->load();
            if (!loaded.has_value())
                return std::unexpected(loaded.error());
            next_sequence_ = std::max<std::uint64_t>(*loaded, 1u);
            reserved_until_ = *next_sequence_;
        }
        const auto sequence = *next_sequence_;
        if (sequence > max_sequence)
            return refuse(error::secure_session_closed);
        // A block is recorded durably before its first number goes out, so a restart resumes past everything sent.
        if ((store_ != nullptr) && (sequence >= reserved_until_))
        {
            const auto limit =
                std::min<std::uint64_t>(sequence + std::max<std::uint32_t>(configuration_.reservation_block, 1u), max_sequence + 1u);
            if (const auto reserved = store_->reserve_until(limit); !reserved.has_value())
                return std::unexpected(reserved.error());
            reserved_until_ = limit;
        }
        next_sequence_ = sequence + 1u;
        return sequence;
    }

    /// @brief Returns control field 1 with the frame type a data length requires: standard up to 15 octets, extended past.
    [[nodiscard]] static std::uint8_t frame_type(const std::uint8_t control_field_1, const std::size_t data_length) noexcept
    {
        return (data_length <= cemi::max_standard_data_length) ?
                   static_cast<std::uint8_t>(control_field_1 | cemi_frame::standard_frame_mask) :
                   static_cast<std::uint8_t>(control_field_1 & ~cemi_frame::standard_frame_mask);
    }

    expected_byte_buffer_t context::build_secured(const frame_view& frame, const cspan_uint8_t plain_cemi, const secure::secret_key& key,
                                                  const frame_binding& binding, const std::uint64_t sequence) const noexcept(false)
    {
        const auto link = frame.link_offset;
        const std::size_t data_length = frame.data_length + 2u + secured_apdu_overhead;
        byte_buffer_t secured(link + apdu_offset + 1u + data_length, 0u);
        std::copy_n(plain_cemi.begin(), link + apdu_offset, secured.begin());
        secured[link] = frame_type(frame.control_field_1, data_length);
        secured[link + 2u] = static_cast<std::uint8_t>(binding.source.value() >> 8u);
        secured[link + 3u] = static_cast<std::uint8_t>(binding.source.value() & 0xFFu);
        secured[link + 6u] = static_cast<std::uint8_t>(data_length);
        secured[link + apdu_offset] = static_cast<std::uint8_t>(binding.transport_control | secure_service_high);
        secured[link + apdu_offset + 1u] = secure_service_low;

        // The plain APDU is bound without its transport control bits, which B0 takes from the binding.
        std::array<std::uint8_t, max_plain_apdu> plain {};
        plain[0u] = static_cast<std::uint8_t>(plain_cemi[link + apdu_offset] & ~cemi::tpci_mask);
        std::copy_n(plain_cemi.begin() + static_cast<std::ptrdiff_t>(link + apdu_offset + 1u), frame.data_length, plain.begin() + 1);
        const span_uint8_t destination {secured.data() + link + apdu_offset + 2u, secured.size() - link - apdu_offset - 2u};
        const auto sealed = seal_apdu(destination, key, security_control {.algorithm = configuration_.outgoing}, sequence, binding,
                                      {plain.data(), frame.data_length + 1u});
        if (!sealed.has_value())
            return std::unexpected(sealed.error());
        return secured;
    }

    expected_byte_buffer_t context::secure_frame(const cspan_uint8_t plain_cemi) noexcept(false)
    {
        const auto frame = view(plain_cemi);
        const std::lock_guard lock {mutex_};
        if (!valid_)
            return refuse(error::invalid_configuration);
        // Point-to-point traffic, and group traffic to a group without a key, goes out as it is.
        const auto* const key = (frame.has_value() && frame->group) ? key_for(frame->binding.destination) : nullptr;
        if (key == nullptr)
            return byte_buffer_t(plain_cemi.begin(), plain_cemi.end());
        // A frame with no APCI - a transport control frame - carries nothing S-A_Data can secure.
        if (frame->secured || (frame->binding.transport_control != cemi::tpci_unnumbered_data) || (frame->data_length == 0u))
            return refuse(error::secure_unsupported);
        if ((frame->data_length + 2u + secured_apdu_overhead) > apdu_payload::max_octets)
            return refuse(error::payload_too_large);

        auto binding = frame->binding;
        if (binding.source.value() == 0u)
            binding.source = configuration_.local_address;
        if (binding.source.value() == 0u)
            return refuse(error::invalid_configuration);
        const auto sequence = next_sequence();
        if (!sequence.has_value())
            return std::unexpected(sequence.error());
        return build_secured(*frame, plain_cemi, *key, binding, *sequence);
    }

    std::expected<const secure::secret_key*, std::error_code> context::admit_secured(const frame_view& frame,
                                                                                     const cspan_uint8_t secured) noexcept
    {
        const auto control = secured.empty() ? refuse(error::malformed_frame) : decode_security_control(secured[0u]);
        if (!frame.group || !control.has_value() || control->tool_access || control->system_broadcast ||
            (control->service != security_service::data))
        {
            ++counters_.refused_services;
            return refuse(error::secure_unsupported);
        }
        const auto* const key = key_for(frame.binding.destination);
        if ((key == nullptr) || !sender_allowed(frame.binding.source, frame.binding.destination))
        {
            ++counters_.missing_keys;
            return refuse(error::secure_key_missing);
        }
        return key;
    }

    expected_void_t context::accept_sequence(const frame_view& frame, const std::uint64_t sequence) noexcept
    {
        // A confirmation carries a telegram this endpoint sent, under its own sequence number.
        if (frame.code == cemi_message_code::l_data_con)
            return {};
        const auto sender = std::ranges::find(configuration_.senders, frame.binding.source, &sender_sequence::address);
        if (sender == configuration_.senders.end())
        {
            ++counters_.missing_keys;
            return refuse(error::secure_key_missing);
        }
        if (sequence <= sender->last_valid_sequence)
        {
            ++counters_.replays;
            return refuse(error::secure_replay);
        }
        sender->last_valid_sequence = sequence;
        return {};
    }

    expected_byte_buffer_t context::open_secured(const frame_view& frame, const cspan_uint8_t cemi) noexcept(false)
    {
        const cspan_uint8_t secured {cemi.data() + frame.link_offset + apdu_offset + 2u, frame.data_length - 1u};
        const auto key = admit_secured(frame, secured);
        if (!key.has_value())
            return std::unexpected(key.error());
        std::array<std::uint8_t, max_plain_apdu> plain {};
        const auto opened = open_apdu(plain, **key, frame.binding, secured);
        if (!opened.has_value() && (opened.error() == make_error_code(error::secure_authentication_failed)))
            ++counters_.authentication_failures;
        if (!opened.has_value())
            return std::unexpected(opened.error());
        // The MAC has verified, so the sequence number can be believed; the sender table changes only now (P2).
        if (const auto accepted = accept_sequence(frame, sequence_of(secured)); !accepted.has_value() || (*opened < cemi::apci_size))
        {
            secure::detail::cleanse(plain);
            return accepted.has_value() ? refuse(error::malformed_frame) : std::unexpected(accepted.error());
        }

        const auto link = frame.link_offset;
        const auto data_length = *opened - 1u;
        byte_buffer_t result(link + apdu_offset + 1u + data_length, 0u);
        std::copy_n(cemi.begin(), link + apdu_offset, result.begin());
        result[link] = frame_type(frame.control_field_1, data_length);
        result[link + 6u] = static_cast<std::uint8_t>(data_length);
        result[link + apdu_offset] = static_cast<std::uint8_t>(frame.binding.transport_control | (plain[0u] & ~cemi::tpci_mask));
        std::copy_n(plain.begin() + 1, data_length, result.begin() + static_cast<std::ptrdiff_t>(link + apdu_offset + 1u));
        return result;
    }

    expected_byte_buffer_t context::open_frame(const cspan_uint8_t cemi) noexcept(false)
    {
        const auto frame = view(cemi);
        const std::lock_guard lock {mutex_};
        if (!valid_)
            return refuse(error::invalid_configuration);
        if (frame.has_value() && frame->secured)
            return open_secured(*frame, cemi);
        // A group address with a key takes nothing unsecured (P1); everything else passes as it is.
        if (frame.has_value() && frame->group && (key_for(frame->binding.destination) != nullptr))
        {
            ++counters_.unencrypted_refused;
            return refuse(error::secure_frame_required);
        }
        return byte_buffer_t(cemi.begin(), cemi.end());
    }
}
