/// @file api/kmx/aio/knx/data_secure/context.hpp
/// @brief The KNX Data Secure state of one endpoint, and the policy it applies to whole cEMI frames.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// @ref kmx::aio::knx::data_secure::context is the policy a tunnelling client or a router applies to whole cEMI frames:
/// - a group telegram to a group address that has a key is secured on the way out, and required to arrive secured (P1);
///   other traffic passes unchanged, as it does through xknx;
/// - a received telegram's MAC is verified before anything else it carries is believed. Only then is its sender looked
///   up, and its sequence number compared with the last one accepted from that sender, which it has to exceed (P2);
/// - S-A_Sync, tool access, system broadcast and point-to-point Data Secure are refused as unsupported;
/// - outgoing sequence numbers are reserved in blocks through a @ref kmx::aio::knx::data_secure::sequence_store before
///   any of them is sent, so a restart never sends one twice.
/// @reference KNX System Specifications 03/03/07 "Application Layer", security; xknx 3.20.0 `xknx/secure/data_secure.py`.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/data_secure.hpp>
        #include <kmx/aio/knx/data_secure/sequence_store.hpp>
        #include <kmx/aio/knx/group_address.hpp>
        #include <kmx/aio/knx/individual_address.hpp>
        #include <kmx/aio/knx/secure/common.hpp>
        #include <kmx/aio/knx/secure/key.hpp>

        #include <cstdint>
        #include <expected>
        #include <mutex>
        #include <optional>
        #include <system_error>
    #endif

namespace kmx::aio::knx::data_secure
{
    /// @brief The Data Secure state of one endpoint: its keys, the senders it trusts, and its outgoing sequence numbers.
    /// @note Every member takes the context's lock for its own duration, so one context may serve a sending and a
    ///       receiving task on different threads.
    class context final
    {
    public:
        /// @brief Creates the context.
        /// @param value The configuration.
        /// @param store Where outgoing sequence numbers are reserved; must outlive the context. Without one, the first
        ///        sequence number is the number of milliseconds since @ref sequence_epoch_ms.
        /// @param wall_clock Milliseconds since the Unix epoch; the system clock when null.
        explicit context(configuration value, sequence_store* store = nullptr,
                         secure::wall_clock_ms_function wall_clock = nullptr) noexcept;

        context(const context&) = delete;
        context& operator=(const context&) = delete;

        /// @brief Secures an outgoing cEMI frame when it is a group telegram to a group address with a key.
        /// @param plain_cemi A cEMI message; anything but such a telegram is returned unchanged.
        /// @return The frame to send.
        /// @retval kmx::aio::knx::error::invalid_configuration The configuration exceeds its limits, or neither the frame
        ///         nor the configuration names a source address.
        /// @retval kmx::aio::knx::error::secure_unsupported The telegram is secured already, or its transport control is
        ///         not unnumbered data.
        /// @retval kmx::aio::knx::error::payload_too_large The secured telegram would exceed the longest APDU.
        /// @retval kmx::aio::knx::error::secure_session_closed The sequence numbers are spent.
        /// @note A failure of the @ref sequence_store is returned as it reported it, and nothing is sent.
        [[nodiscard]] expected_byte_buffer_t secure_frame(cspan_uint8_t plain_cemi) noexcept(false);

        /// @brief Opens a received cEMI frame when it is secured, and refuses an unsecured one to a group with a key.
        /// @param cemi A cEMI message; anything else is returned unchanged.
        /// @return The plain frame.
        /// @retval kmx::aio::knx::error::secure_frame_required An unsecured telegram to a group address with a key (P1).
        /// @retval kmx::aio::knx::error::secure_unsupported Point-to-point Data Secure, S-A_Sync, tool access or system
        ///         broadcast.
        /// @retval kmx::aio::knx::error::secure_key_missing No key for the group, or a sender not trusted, or not allowed
        ///         for the group.
        /// @retval kmx::aio::knx::error::secure_authentication_failed The MAC does not verify.
        /// @retval kmx::aio::knx::error::secure_replay A sequence number not above the sender's last accepted one.
        /// @note Every refusal is counted. An L_Data.con confirms a telegram this endpoint sent, so its sequence number is
        ///       not held against the sender table.
        [[nodiscard]] expected_byte_buffer_t open_frame(cspan_uint8_t cemi) noexcept(false);

        /// @brief Indicates whether a group address has a key, so that its telegrams travel secured.
        [[nodiscard]] bool secured_group(group_address address) const noexcept;

        /// @brief Returns a copy of what the context has refused.
        [[nodiscard]] secure::statistics counters() const noexcept;

    private:
        /// @brief An L_Data frame's layout and addressing, read without decoding its APDU.
        struct frame_view;

        [[nodiscard]] static std::optional<frame_view> view(cspan_uint8_t cemi) noexcept;
        [[nodiscard]] const secure::secret_key* key_for(std::uint16_t group) const noexcept;
        [[nodiscard]] bool sender_allowed(individual_address source, std::uint16_t group) const noexcept;
        [[nodiscard]] std::uint64_t starting_sequence() const noexcept;
        [[nodiscard]] std::expected<std::uint64_t, std::error_code> next_sequence() noexcept;
        /// @brief Builds the secured copy of a frame, under the source address its binding names.
        [[nodiscard]] expected_byte_buffer_t build_secured(const frame_view& frame, cspan_uint8_t plain_cemi, const secure::secret_key& key,
                                                           std::uint64_t sequence) const noexcept(false);
        [[nodiscard]] std::expected<const secure::secret_key*, std::error_code> admit_secured(const frame_view& frame,
                                                                                              cspan_uint8_t secured) noexcept;
        [[nodiscard]] expected_void_t accept_sequence(const frame_view& frame, std::uint64_t sequence) noexcept;
        [[nodiscard]] expected_byte_buffer_t open_secured(const frame_view& frame, cspan_uint8_t cemi) noexcept(false);

        configuration configuration_;
        sequence_store* store_ {};
        secure::wall_clock_ms_function wall_clock_ {};
        bool valid_ {};
        /// @brief Guards everything below.
        mutable std::mutex mutex_ {};
        std::optional<std::uint64_t> next_sequence_ {};
        std::uint64_t reserved_until_ {};
        secure::statistics counters_ {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
