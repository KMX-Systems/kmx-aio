/// @file src/kmx/aio/knx/data_secure/context.cpp
/// @brief The compiled body of the KNX Data Secure context: whole cEMI frames secured, opened and refused.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/data_secure/context.hpp>
#ifndef PCH
    #include <kmx/aio/knx/apdu_payload.hpp>
    #include <kmx/aio/knx/cemi.hpp>
    #include <kmx/aio/knx/cemi_frame.hpp>
    #include <kmx/aio/knx/data_secure/detail/common.hpp>
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/keyring/document.hpp>
    #include <kmx/aio/knx/secure/secret_bytes.hpp>

    #include <algorithm>
    #include <array>
    #include <chrono>
    #include <cstddef>
    #include <utility>
#endif

namespace kmx::aio::knx::data_secure
{
    /// @brief Where the first APDU octet sits, past the link header.
    static constexpr std::size_t apdu_offset = cemi::link_header_size;

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
        value.secured = (value.data_length >= 2u) &&
                        ((cemi[link + apdu_offset] & detail::service_apci_high) == detail::service_apci_high) &&
                        (cemi[link + apdu_offset + 1u] == detail::service_apci_low);
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
            return detail::refuse(error::secure_session_closed);
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
                                                  const std::uint64_t sequence) const noexcept(false)
    {
        const auto& binding = frame.binding;
        const auto link = frame.link_offset;
        const std::size_t data_length = frame.data_length + 2u + secured_apdu_overhead;
        byte_buffer_t secured(link + apdu_offset + 1u + data_length, 0u);
        std::copy_n(plain_cemi.begin(), link + apdu_offset, secured.begin());
        secured[link] = frame_type(frame.control_field_1, data_length);
        secured[link + 2u] = static_cast<std::uint8_t>(binding.source.value() >> 8u);
        secured[link + 3u] = static_cast<std::uint8_t>(binding.source.value() & 0xFFu);
        secured[link + 6u] = static_cast<std::uint8_t>(data_length);
        secured[link + apdu_offset] = static_cast<std::uint8_t>(binding.transport_control | detail::service_apci_high);
        secured[link + apdu_offset + 1u] = detail::service_apci_low;

        // The plain APDU is bound without its transport control bits, which B0 takes from the binding.
        std::array<std::uint8_t, max_plain_apdu> plain {};
        plain[0u] = static_cast<std::uint8_t>(plain_cemi[link + apdu_offset] & ~cemi::tpci_mask);
        std::copy_n(plain_cemi.begin() + static_cast<std::ptrdiff_t>(link + apdu_offset + 1u), frame.data_length, plain.begin() + 1);
        const span_uint8_t destination {secured.data() + link + apdu_offset + 2u, secured.size() - link - apdu_offset - 2u};
        const auto sealed =
            seal_apdu(destination, key, {.control = {.algorithm = configuration_.outgoing}, .sequence = sequence, .binding = binding},
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
            return detail::refuse(error::invalid_configuration);
        // Point-to-point traffic, and group traffic to a group without a key, goes out as it is.
        const auto* const key = (frame.has_value() && frame->group) ? key_for(frame->binding.destination) : nullptr;
        if (key == nullptr)
            return byte_buffer_t(plain_cemi.begin(), plain_cemi.end());
        // A frame with no APCI - a transport control frame - carries nothing S-A_Data can secure.
        if (frame->secured || (frame->binding.transport_control != cemi::tpci_unnumbered_data) || (frame->data_length == 0u))
            return detail::refuse(error::secure_unsupported);
        if ((frame->data_length + 2u + secured_apdu_overhead) > apdu_payload::max_octets)
            return detail::refuse(error::payload_too_large);

        // The frame goes out under the endpoint's own address when it names none.
        auto outgoing = *frame;
        if (outgoing.binding.source.value() == 0u)
            outgoing.binding.source = configuration_.local_address;
        if (outgoing.binding.source.value() == 0u)
            return detail::refuse(error::invalid_configuration);
        const auto sequence = next_sequence();
        if (!sequence.has_value())
            return std::unexpected(sequence.error());
        return build_secured(outgoing, plain_cemi, *key, *sequence);
    }

    std::expected<const secure::secret_key*, std::error_code> context::admit_secured(const frame_view& frame,
                                                                                     const cspan_uint8_t secured) noexcept
    {
        const auto control = secured.empty() ? detail::refuse(error::malformed_frame) : decode_security_control(secured[0u]);
        if (!frame.group || !control.has_value() || control->tool_access || control->system_broadcast ||
            (control->service != security_service::data))
        {
            ++counters_.refused_services;
            return detail::refuse(error::secure_unsupported);
        }

        const auto* const key = key_for(frame.binding.destination);
        if ((key == nullptr) || !sender_allowed(frame.binding.source, frame.binding.destination))
        {
            ++counters_.missing_keys;
            return detail::refuse(error::secure_key_missing);
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
            return detail::refuse(error::secure_key_missing);
        }

        if (sequence <= sender->last_valid_sequence)
        {
            ++counters_.replays;
            return detail::refuse(error::secure_replay);
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
            return accepted.has_value() ? detail::refuse(error::malformed_frame) : std::unexpected(accepted.error());
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
            return detail::refuse(error::invalid_configuration);
        if (frame.has_value() && frame->secured)
            return open_secured(*frame, cemi);
        // A group address with a key takes nothing unsecured (P1); everything else passes as it is.
        if (frame.has_value() && frame->group && (key_for(frame->binding.destination) != nullptr))
        {
            ++counters_.unencrypted_refused;
            return detail::refuse(error::secure_frame_required);
        }

        return byte_buffer_t(cemi.begin(), cemi.end());
    }
}
