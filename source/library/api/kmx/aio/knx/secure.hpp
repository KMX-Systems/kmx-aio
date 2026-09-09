/// @file aio/knx/secure.hpp
/// @brief Capability and policy boundary for KNX Secure profiles.
/// @details
/// This header is the seam KNX Secure would be implemented behind, and an envelope that keeps the seam
/// exercised until it is. It carries the parts that are cryptography-independent - profile selection, key
/// configuration, the replay window, and a framing that names a sequence number and a payload - and
/// delegates every actual cryptographic operation to a caller-supplied @ref
/// kmx::aio::knx::secure::provider.
/// @warning This build implements no KNX Secure profile. There is no key agreement, no AES-CCM and no
///          message authentication code here, and the envelope is deliberately not the KNX Secure wire
///          format; see the warning on @ref kmx::aio::knx::secure::secure_service for why its service type
///          is an unassigned one. A configuration selecting a profile is only as strong as the provider
///          supplied with it.
/// @reference KNX System Specifications, 03/08/09 "KNXnet/IP Security".
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <array>
        #include <cstdint>
        #include <expected>
        #include <span>
        #include <system_error>
        #include <vector>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/error.hpp>

namespace kmx::aio::knx::secure
{
    /// @brief Service type of this build's placeholder secure envelope.
    /// @warning This is **not** KNX SECURE_WRAPPER, and the envelope below is not the KNX Secure wire
    ///          format. There is no key agreement, no AES-CCM, and no message authentication code here:
    ///          @ref provider is an injection point for cryptography that this build does not supply.
    /// @details The value is deliberately outside every assigned KNXnet/IP service family. It used to be
    ///          `0x0950`, which *is* SECURE_WRAPPER - so a frame this code emitted was not merely
    ///          non-standard, it invited a real KNX Secure device to parse an unauthenticated payload as
    ///          an authenticated one. An unassigned identifier makes a conforming peer reject the datagram
    ///          as an unknown service instead, which is the correct outcome until a real profile ships.
    /// @reference KNX System Specifications, 03/08/09 "KNXnet/IP Security", for the format this is not.
    inline constexpr std::uint16_t secure_service = 0xFF00u;
    /// @brief The real SECURE_WRAPPER service type, named so the codec can refuse to answer to it.
    inline constexpr std::uint16_t knx_secure_wrapper_service = 0x0950u;
    /// @brief Size of the envelope header that follows the KNXnet/IP header, in octets.
    /// @details Profile, one reserved octet, an eight-octet sequence number, then the payload length.
    inline constexpr std::size_t secure_packet_header_size = 12u;

    /// @brief Width of a KNX Secure key, in octets.
    inline constexpr std::size_t key_size = 16u;
    /// @brief A KNX Secure key.
    /// @details Defined here rather than beside the keyring that reads them, because the keyring exists to
    ///          supply what this boundary consumes: a key recovered from a keyring is assignable into
    ///          @ref configuration by construction, instead of by both spellings happening to name the same
    ///          array. @ref kmx::aio::knx::keyring::key_t is this type.
    using key_t = std::array<std::uint8_t, key_size>;

    /// @brief Which KNX Secure profile a connection is configured for.
    /// @details The two profiles protect different things and are not alternatives to each other: IP Secure
    ///          protects the KNXnet/IP datagram between two IP devices, Data Secure protects the application
    ///          payload end to end, so a frame relayed onto the bus stays protected past the gateway.
    enum class profile : std::uint8_t
    {
        /// @brief Unprotected. The provider is not consulted and no envelope is applied.
        none,
        /// @brief Protection of the KNXnet/IP datagram, hop by hop.
        ip_secure,
        /// @brief Protection of the application payload, end to end.
        data_secure,
    };

    /// @brief What to do with a sequence number that is not strictly newer than the highest one seen.
    enum class replay_policy : std::uint8_t
    {
        /// @brief Accept only strictly increasing sequence numbers.
        reject,
        /// @brief Accept an older sequence number that falls inside the window and has not been seen.
        /// @note The tolerance a lossy or reordering network needs; it requires a non-zero window.
        accept_within_window,
    };

    /// @brief The secure profile, replay policy and key material one connection is configured with.
    /// @note @ref validate rejects the combinations that cannot mean anything, so a caller need not check
    ///       the fields against each other itself.
    struct configuration
    {
        /// @brief The profile to apply; @ref profile::none leaves the connection unprotected.
        profile selected = profile::none;
        /// @brief How to treat a sequence number that is not the newest.
        replay_policy replay = replay_policy::reject;
        /// @brief How far back @ref replay_policy::accept_within_window reaches; zero otherwise.
        /// @note Capped at 64 by @ref replay_window_state, which tracks the window as a bitmask.
        std::uint32_t replay_window {};
        /// @brief The key the provider is expected to use; must not be all zero when a profile is selected.
        key_t key {};
    };

    /// @brief One decoded secure envelope: which profile framed it, its sequence, and what it carried.
    struct packet
    {
        /// @brief The profile the sender framed the packet under.
        profile selected = profile::none;
        /// @brief The sender's sequence number, which the replay window is checked against.
        std::uint64_t sequence {};
        /// @brief The protected payload, exactly as the provider produced or will consume it.
        byte_buffer_t payload {};
    };

    /// @brief Checks that a secure configuration is internally consistent.
    /// @param value The configuration to check.
    /// @return Nothing, or @ref kmx::aio::knx::error::invalid_configuration.
    /// @details An unprotected configuration is always valid and nothing else is examined. Otherwise the
    ///          window has to agree with the policy - a window of zero cannot accept anything within it,
    ///          and a non-zero window under @ref replay_policy::reject is a setting that does nothing - and
    ///          the key must not be all zero, which is what an uninitialised @ref configuration looks like.
    [[nodiscard]] constexpr std::expected<void, error> validate(
        const configuration& value) noexcept
    {
        if (value.selected == profile::none)
            return {};
        if ((value.replay == replay_policy::accept_within_window) && (value.replay_window == 0u))
            return std::unexpected(error::invalid_configuration);
        if ((value.replay == replay_policy::reject) && (value.replay_window != 0u))
            return std::unexpected(error::invalid_configuration);
        bool has_key {};
        for (const auto byte: value.key)
            has_key = has_key || (byte != 0u);
        if (!has_key)
            return std::unexpected(error::invalid_configuration);
        return {};
    }

    /// @brief Sliding window of accepted sequence numbers, for rejecting replayed packets.
    /// @details The newest sequence number seen, plus a 64-bit mask of which of the preceding 64 have
    ///          already arrived. A packet is accepted once: newer than everything seen, or inside the
    ///          window and not yet marked. The mask is what makes an out-of-order delivery acceptable
    ///          without making a replay of it acceptable too.
    /// @warning The window is admitted to only after the payload has been authenticated - see
    ///          @ref unprotect_packet. A sequence number from an unverified packet is attacker-chosen, and
    ///          advancing the window on one is enough to lock out every genuine packet that follows.
    class replay_window_state
    {
    public:
        /// @brief Creates a window state.
        /// @param window How far back an out-of-order sequence number may be; zero admits none.
        /// @note Values above 64 are indistinguishable from 64: the mask holds no more than that.
        constexpr explicit replay_window_state(const std::uint32_t window = 0u) noexcept:
            window_(window) {}

        /// @brief Admits one sequence number to the window.
        /// @param sequence The sequence number carried by an already-authenticated packet.
        /// @return `true` when the packet is fresh, `false` when it is a replay or too old.
        /// @note Accepting mutates the window, so this must be called once per packet.
        [[nodiscard]] constexpr bool accept(const std::uint64_t sequence) noexcept
        {
            if (!initialized_)
            {
                initialized_ = true;
                highest_ = sequence;
                seen_ = 1u;
                return true;
            }

            if (sequence > highest_)
            {
                const auto advance = sequence - highest_;
                if (advance >= 64u)
                    seen_ = 1u;
                else
                    seen_ = (seen_ << advance) | 1u;
                highest_ = sequence;
                return true;
            }

            const auto distance = highest_ - sequence;
            if ((window_ == 0u) || (distance >= window_) || (distance >= 64u))
                return false;
            const auto bit = std::uint64_t {1u} << distance;
            if ((seen_ & bit) != 0u)
                return false;
            seen_ |= bit;
            return true;
        }

        /// @brief Indicates whether any sequence number has been admitted yet.
        /// @note The first packet is accepted whatever its sequence number, because there is nothing to
        ///       compare it against; a peer picks where its own counter starts.
        [[nodiscard]] constexpr bool initialized() const noexcept { return initialized_; }

        /// @brief Returns the highest sequence number admitted so far; zero when none has been.
        [[nodiscard]] constexpr std::uint64_t highest() const noexcept { return highest_; }

    private:
        std::uint32_t window_ {};
        std::uint64_t highest_ {};
        std::uint64_t seen_ {};
        bool initialized_ {};
    };

    /// @brief The caller-supplied cryptography a secure profile is built on.
    /// @details The whole of the confidentiality and authenticity guarantee lives behind these two calls;
    ///          everything else in this header is framing and policy around them. An implementation holds
    ///          the key from @ref configuration and binds @p sequence into whatever it computes, so that a
    ///          packet replayed under a different sequence number fails to verify rather than merely being
    ///          caught by the window.
    /// @warning This build supplies no implementation. Without one, a selected profile frames traffic but
    ///          protects nothing.
    class provider
    {
    public:
        /// @brief Constructs a provider.
        provider() noexcept = default;
        provider(const provider&) = delete;
        provider& operator=(const provider&) = delete;
        /// @brief Destroys the provider.
        virtual ~provider() noexcept = default;

        /// @brief Protects one outgoing payload.
        /// @param packet The payload to protect.
        /// @param sequence The sequence number this payload is sent under.
        /// @return The protected octets, or the reason they could not be produced.
        [[nodiscard]] virtual expected_byte_buffer_t protect(cspan_uint8_t packet, std::uint64_t sequence) noexcept = 0;

        /// @brief Verifies and recovers one incoming payload.
        /// @param packet The protected octets, as they arrived.
        /// @param sequence The sequence number the envelope declared.
        /// @return The recovered payload, or the reason verification failed.
        /// @note Returning a value is what marks the packet authentic, and only then is @p sequence
        ///       admitted to the replay window.
        [[nodiscard]] virtual expected_byte_buffer_t unprotect(cspan_uint8_t packet, std::uint64_t sequence) noexcept = 0;
    };

    /// @brief Encodes one secure envelope, KNXnet/IP header included.
    /// @param destination The destination octets; must hold header, envelope header and payload.
    /// @param value The envelope to encode; its payload is written as given.
    /// @return Nothing, or the reason it could not be encoded.
    /// @retval kmx::aio::knx::error::invalid_configuration @p value selects @ref profile::none.
    /// @retval kmx::aio::knx::error::invalid_length The frame would exceed the KNXnet/IP maximum, or
    ///         @p destination is too small for it.
    [[nodiscard]] expected_void_t encode_secure_packet(span_uint8_t destination, const packet& value) noexcept;

    /// @brief Decodes one secure envelope.
    /// @param packet_bytes The received octets, header included.
    /// @return The decoded envelope, or the reason it could not be read.
    /// @retval kmx::aio::knx::error::secure_unsupported The datagram is a real KNX SECURE_WRAPPER, which
    ///         this build declines to answer to rather than parsing under this envelope's layout.
    /// @retval kmx::aio::knx::error::unsupported_service The service type is not this envelope's, or the
    ///         profile octet names none this build implements.
    /// @retval kmx::aio::knx::error::malformed_frame The declared lengths do not agree with the octets.
    /// @note The payload is returned exactly as it arrived; nothing is verified until it is handed to a
    ///       @ref provider.
    [[nodiscard]] std::expected<packet, std::error_code> decode_secure_packet(
        cspan_uint8_t packet_bytes) noexcept;

    /// @brief Protects a payload and wraps it in a complete secure datagram.
    /// @param crypto The provider that performs the protection.
    /// @param selected The profile to frame under; must not be @ref profile::none.
    /// @param payload The payload to protect.
    /// @param sequence The sequence number to send under; must not repeat for a given key.
    /// @return The complete datagram, or the reason it could not be produced.
    /// @retval kmx::aio::knx::error::invalid_configuration @p selected is @ref profile::none.
    [[nodiscard]] expected_byte_buffer_t protect_packet(
        provider& crypto,
        profile selected,
        cspan_uint8_t payload,
        std::uint64_t sequence) noexcept;

    /// @brief Decodes a secure datagram, verifies it, and admits its sequence number to the replay window.
    /// @param crypto The provider that performs the verification.
    /// @param expected_profile The profile the connection is configured for; must not be
    ///        @ref profile::none.
    /// @param packet_bytes The received octets, header included.
    /// @param replay The window to check the sequence number against; null skips the replay check.
    /// @return The recovered payload, or the reason it was rejected.
    /// @retval kmx::aio::knx::error::invalid_configuration @p expected_profile is @ref profile::none, or
    ///         the datagram was framed under a different profile than the connection expects.
    /// @retval kmx::aio::knx::error::sequence_error The packet is authentic but its sequence number was
    ///         already seen, or falls outside the window.
    /// @details Verification happens before the window is touched, and the ordering is load-bearing: the
    ///          sequence number is attacker-controlled until the provider has verified the frame carrying
    ///          it, so admitting it first would let one forged datagram near the top of the range move the
    ///          window past every value a genuine peer will send. The cost is decrypting a duplicate, which
    ///          is the cheaper of the two failures.
    [[nodiscard]] expected_byte_buffer_t unprotect_packet(
        provider& crypto,
        profile expected_profile,
        cspan_uint8_t packet_bytes,
        replay_window_state* replay = nullptr) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
