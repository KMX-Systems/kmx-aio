/// @file kmx/aio/knx/data_secure.hpp
/// @brief KNX Data Secure for group communication: cEMI group telegrams secured end to end.
/// @details
/// A Data Secure telegram carries A_SecureService - APCI 0x3F1 - in place of its APDU: a security control field, a 48-bit
/// sequence number, the APDU, and a 4-octet MAC under the key of the destination group address. With authenticated
/// encryption the APDU and the MAC are encrypted; with authentication only both travel in the clear. The MAC binds the
/// sequence number, both addresses, the address type and extended frame format, and the transport control bits.
///
/// @ref kmx::aio::knx::data_secure::seal_apdu and @ref kmx::aio::knx::data_secure::open_apdu are the codec, for either
/// algorithm and any destination. @ref kmx::aio::knx::data_secure::context is the policy a tunnelling client or a router
/// applies to whole cEMI frames:
/// - a group telegram to a group address that has a key is secured on the way out, and required to arrive secured (P1);
///   other traffic passes unchanged, as it does through xknx;
/// - a received telegram's MAC is verified before anything else it carries is believed. Only then is its sender looked
///   up, and its sequence number compared with the last one accepted from that sender, which it has to exceed (P2);
/// - S-A_Sync, tool access, system broadcast and point-to-point Data Secure are refused as unsupported;
/// - outgoing sequence numbers are reserved in blocks through a @ref kmx::aio::knx::data_secure::sequence_store before
///   any of them is sent, so a restart never sends one twice.
/// @reference KNX System Specifications 03/03/07 "Application Layer", security; xknx 3.20.0 `xknx/secure/data_secure.py`.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <cstddef>
        #include <cstdint>
        #include <expected>
        #include <mutex>
        #include <optional>
        #include <system_error>
        #include <vector>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/address.hpp>
    #include <kmx/aio/knx/keyring.hpp>
    #include <kmx/aio/knx/secure/common.hpp>
    #include <kmx/aio/knx/secure/key.hpp>

namespace kmx::aio::knx::data_secure
{
    /// @brief The APCI of A_SecureService.
    inline constexpr std::uint16_t secure_service_apci = 0x03F1u;
    /// @brief The length of a Data Secure MAC, in octets.
    inline constexpr std::size_t mac_size = 4u;
    /// @brief The length of a Data Secure sequence number, in octets.
    inline constexpr std::size_t sequence_size = 6u;
    /// @brief What secured data holds beyond the plain APDU: the security control field, the sequence number and the MAC.
    inline constexpr std::size_t secured_apdu_overhead = 1u + sequence_size + mac_size;
    /// @brief The longest plain APDU the codec takes: its length enters B0 as one octet.
    inline constexpr std::size_t max_plain_apdu = 0xFFu;
    /// @brief The largest sequence number: 48 bits.
    inline constexpr std::uint64_t max_sequence = (std::uint64_t {1u} << 48u) - 1u;
    /// @brief 2018-01-05T00:00:00Z in milliseconds since the Unix epoch, from which time-based starting sequence numbers count.
    inline constexpr std::uint64_t sequence_epoch_ms = 1'515'110'400'000u;
    /// @brief The most group keys, and the most senders, a configuration may hold.
    inline constexpr std::size_t max_table_entries = 4096u;

    /// @brief How a telegram is protected.
    enum class algorithm : std::uint8_t
    {
        /// @brief The APDU and the MAC travel in the clear; the MAC covers the APDU.
        authentication_only = 0b000u,
        /// @brief The APDU and the MAC are encrypted.
        authenticated_encryption = 0b001u,
    };

    /// @brief The security service a telegram carries.
    enum class security_service : std::uint8_t
    {
        /// @brief S-A_Data: secured application data.
        data = 0b000u,
        /// @brief S-A_Sync_Req.
        sync_request = 0b010u,
        /// @brief S-A_Sync_Res.
        sync_response = 0b011u,
    };

    /// @brief A security control field.
    struct security_control
    {
        /// @brief Whether the telegram is secured under a tool key rather than a group key.
        bool tool_access {};
        /// @brief How the telegram is protected.
        data_secure::algorithm algorithm {data_secure::algorithm::authenticated_encryption};
        /// @brief Whether the telegram is a system broadcast.
        bool system_broadcast {};
        /// @brief The security service.
        security_service service {security_service::data};
    };

    /// @brief A security control field, or why an octet is not one this build knows.
    using security_control_result_t = std::expected<security_control, std::error_code>;

    /// @brief Encodes a security control field.
    [[nodiscard]] std::uint8_t encode_security_control(const security_control& value) noexcept;

    /// @brief Decodes a security control field.
    /// @return The field, or @ref kmx::aio::knx::error::secure_unsupported for an algorithm or service this build does not
    ///         know.
    [[nodiscard]] security_control_result_t decode_security_control(std::uint8_t octet) noexcept;

    /// @brief The link-layer fields a secured APDU is bound to.
    struct frame_binding
    {
        /// @brief The sender.
        individual_address source {};
        /// @brief The destination, a group or an individual address as control field 2 says.
        std::uint16_t destination {};
        /// @brief Control field 2; its address type bit and extended frame format enter the MAC.
        std::uint8_t control_field_2 {};
        /// @brief The transport control bits of the first APDU octet.
        std::uint8_t transport_control {};
    };

    /// @brief Secures an APDU, writing what follows the APCI: the security control field, the sequence number, the APDU
    ///        and the MAC.
    /// @param destination Receives the secured data; at least `plain_apdu.size() + secured_apdu_overhead` octets.
    /// @param key The key of the destination.
    /// @param control The security control field.
    /// @param sequence The sequence number; @ref max_sequence at most.
    /// @param binding The frame fields the MAC binds.
    /// @param plain_apdu The plain APDU from its first octet, with the transport control bits cleared.
    /// @return The secured data's length.
    /// @retval kmx::aio::knx::error::invalid_length @p destination is too small, or @p plain_apdu longer than
    ///         @ref max_plain_apdu.
    /// @retval kmx::aio::knx::error::invalid_configuration @p sequence exceeds @ref max_sequence.
    /// @retval kmx::aio::knx::error::crypto_failure The backend failed.
    [[nodiscard]] expected_size_t seal_apdu(span_uint8_t destination, const secure::secret_key& key, const security_control& control,
                                            std::uint64_t sequence, const frame_binding& binding, cspan_uint8_t plain_apdu) noexcept;

    /// @brief Verifies secured data and recovers the plain APDU.
    /// @param destination Receives the plain APDU; at least `secured.size() - secured_apdu_overhead` octets.
    /// @param key The key of the destination.
    /// @param binding The frame fields the MAC binds, as received.
    /// @param secured What follows the APCI: the security control field, the sequence number, the APDU and the MAC.
    /// @return The plain APDU's length.
    /// @retval kmx::aio::knx::error::malformed_frame @p secured is shorter than @ref secured_apdu_overhead.
    /// @retval kmx::aio::knx::error::secure_unsupported The security control field names an algorithm or service this
    ///         build does not know.
    /// @retval kmx::aio::knx::error::secure_authentication_failed The MAC does not verify; @p destination is wiped.
    [[nodiscard]] expected_size_t open_apdu(span_uint8_t destination, const secure::secret_key& key, const frame_binding& binding,
                                            cspan_uint8_t secured) noexcept;

    /// @brief Reads the sequence number of secured data, verifying nothing.
    /// @param secured What follows the APCI; at least @ref secured_apdu_overhead octets.
    [[nodiscard]] std::uint64_t sequence_of(cspan_uint8_t secured) noexcept;

    /// @brief Where a sender's outgoing sequence numbers are kept across restarts.
    class sequence_store
    {
    public:
        sequence_store() noexcept = default;
        sequence_store(const sequence_store&) = delete;
        sequence_store& operator=(const sequence_store&) = delete;
        virtual ~sequence_store() noexcept = default;

        /// @brief Returns the first sequence number not yet reserved.
        [[nodiscard]] virtual std::expected<std::uint64_t, std::error_code> load() noexcept = 0;

        /// @brief Durably records that every sequence number below @p limit may be used.
        [[nodiscard]] virtual expected_void_t reserve_until(std::uint64_t limit) noexcept = 0;
    };

    /// @brief A sender a receiver trusts, and the last sequence number it accepted from it.
    struct sender_sequence
    {
        /// @brief The sender.
        individual_address address {};
        /// @brief The last sequence number accepted; the next has to exceed it.
        std::uint64_t last_valid_sequence {};
    };

    /// @brief What a Data Secure endpoint holds.
    /// @note Move-only, because its keys are.
    struct configuration
    {
        /// @brief The endpoint's individual address, used as the source of a frame that names none.
        individual_address local_address {};
        /// @brief The group keys.
        std::vector<keyring::group_key> group_keys {};
        /// @brief The senders telegrams are accepted from.
        std::vector<sender_sequence> senders {};
        /// @brief The senders each group accepts; a group listed here takes telegrams from its listed senders alone, and a
        ///        group not listed from any sender in @ref senders.
        std::vector<keyring::group_senders> allowed_senders {};
        /// @brief How outgoing telegrams are protected.
        data_secure::algorithm outgoing {data_secure::algorithm::authenticated_encryption};
        /// @brief How many sequence numbers are reserved through the @ref sequence_store at a time.
        std::uint32_t reservation_block = 1024u;
    };

    /// @brief A configuration, or why it could not be built.
    using configuration_result_t = std::expected<configuration, std::error_code>;

    /// @brief Builds the Data Secure configuration of one endpoint from a keyring.
    /// @param value The keyring.
    /// @param local_address The endpoint's individual address. When the keyring has an interface there, only the groups
    ///        it receives are keyed, and the senders it lists for them are enforced.
    /// @return The configuration: the group keys, every sender the keyring names with its last recorded sequence number,
    ///         and the allowed senders.
    /// @retval kmx::aio::knx::error::secure_key_missing The keyring holds no group key for the endpoint.
    /// @retval kmx::aio::knx::error::invalid_configuration More than @ref max_table_entries group keys or senders.
    /// @throws std::bad_alloc when the tables cannot be allocated.
    [[nodiscard]] configuration_result_t configuration_for(const keyring::document& value,
                                                           individual_address local_address) noexcept(false);

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
        context(configuration value, sequence_store* store = nullptr, secure::wall_clock_ms_function wall_clock = nullptr) noexcept;

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
        [[nodiscard]] expected_byte_buffer_t build_secured(const frame_view& frame, cspan_uint8_t plain_cemi, const secure::secret_key& key,
                                                           const frame_binding& binding, std::uint64_t sequence) const noexcept(false);
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
