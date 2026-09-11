/// @file api/kmx/aio/knx/secure/wrapper.hpp
/// @brief SECURE_WRAPPER, the frame KNX IP Secure carries every protected KNXnet/IP datagram in.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// A wrapper is the KNXnet/IP header, a two-octet session id, six octets of sequence information, the sender's
/// serial number, a message tag, the encrypted datagram and a sixteen-octet MAC. What it encrypts is a complete
/// KNXnet/IP datagram, header included. A tunnelling session puts its sequence counter in the sequence
/// information; routing puts the sender's timer value there and uses session id zero.
///
/// The codec only moves octets. @ref kmx::aio::knx::secure::seal_wrapper and
/// @ref kmx::aio::knx::secure::open_wrapper apply the KNX CBC-MAC and CTR composition under the session key, or
/// under the backbone key for routing. The MAC covers the wrapper's header and session id as associated data and
/// the plain datagram as payload.
/// @reference KNX System Specifications, 03/08/09 "KNXnet/IP Security"; KNX AN159 "KNXnet/IP Secure".
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/frame.hpp>
        #include <kmx/aio/knx/secure/common.hpp>
        #include <kmx/aio/knx/secure/key.hpp>

        #include <cstddef>
        #include <cstdint>
        #include <expected>
        #include <system_error>
    #endif

namespace kmx::aio::knx::secure
{
    /// @brief Service type of a SECURE_WRAPPER.
    inline constexpr std::uint16_t wrapper_service = 0x0950u;
    /// @brief The session id, sequence information, serial number and message tag ahead of the encrypted datagram.
    inline constexpr std::size_t wrapper_security_header_size = 16u;
    /// @brief Everything a wrapper adds around the datagram it carries: KNXnet/IP header, security header and MAC.
    inline constexpr std::size_t wrapper_overhead = frame::communication_header_size + wrapper_security_header_size + mac_size;
    /// @brief The smallest datagram a wrapper can carry: a KNXnet/IP header and no body.
    inline constexpr std::size_t min_wrapped_frame_size = frame::communication_header_size;
    /// @brief The largest datagram a wrapper of @ref kmx::aio::knx::frame::max_datagram_size octets can carry.
    inline constexpr std::size_t max_wrapped_frame_size = frame::max_datagram_size - wrapper_overhead;

    /// @brief One SECURE_WRAPPER as it travels.
    /// @warning @ref encrypted_frame views the decoded packet, which must outlive this frame.
    struct wrapper_frame
    {
        /// @brief The secure session id; zero for routing.
        std::uint16_t session_id {};
        /// @brief The session's sequence number, or the sender's timer value for routing.
        sequence_information_t sequence {};
        /// @brief The sender's KNX serial number.
        serial_number_t serial_number {};
        /// @brief The message tag; zero for tunnelling.
        message_tag_t message_tag {};
        /// @brief The encrypted datagram.
        cspan_uint8_t encrypted_frame {};
        /// @brief The encrypted MAC.
        mac_t mac {};
    };

    /// @brief The fields a sender chooses for one wrapper.
    struct wrapper_fields
    {
        /// @brief The secure session id; zero for routing.
        std::uint16_t session_id {};
        /// @brief The sequence information.
        sequence_information_t sequence {};
        /// @brief The sender's KNX serial number.
        serial_number_t serial_number {};
        /// @brief The message tag.
        message_tag_t message_tag {};
    };

    /// @brief A decoded wrapper, or why the octets are not one.
    using wrapper_result_t = std::expected<wrapper_frame, std::error_code>;
    /// @brief A KNXnet/IP header, or why a datagram may not travel inside a wrapper.
    using communication_header_result_t = std::expected<communication_header, std::error_code>;

    /// @brief Decodes a SECURE_WRAPPER.
    /// @param packet The received octets, header included.
    /// @return The wrapper, viewing @p packet.
    /// @retval kmx::aio::knx::error::unsupported_service The datagram is another service.
    /// @retval kmx::aio::knx::error::malformed_frame The length disagrees with the datagram, or leaves no room for
    ///         a KNXnet/IP header inside.
    [[nodiscard]] wrapper_result_t decode_wrapper_packet(cspan_uint8_t packet) noexcept;

    /// @brief Encodes a SECURE_WRAPPER from fields that are already encrypted.
    /// @param destination The destination octets; must not overlap @ref wrapper_frame::encrypted_frame.
    /// @param value The wrapper.
    /// @return Nothing, or why the wrapper could not be encoded.
    [[nodiscard]] expected_void_t encode_wrapper_packet(span_uint8_t destination, const wrapper_frame& value) noexcept;

    /// @brief Checks that a plain datagram may travel inside a wrapper.
    /// @param plain_frame The complete KNXnet/IP datagram.
    /// @return Its header.
    /// @retval kmx::aio::knx::error::malformed_frame The header is invalid or its length disagrees with the datagram.
    /// @retval kmx::aio::knx::error::unsupported_service The datagram is a SECURE_WRAPPER itself, or a remote
    ///         diagnosis, configuration or reset service, none of which may be wrapped.
    [[nodiscard]] communication_header_result_t check_wrapped_frame(cspan_uint8_t plain_frame) noexcept;

    /// @brief Encrypts a KNXnet/IP datagram into a complete SECURE_WRAPPER datagram.
    /// @param destination Receives the wrapper; must not overlap @p plain_frame.
    /// @param key The session key, or the backbone key for routing.
    /// @param fields The session id, sequence information, serial number and message tag.
    /// @param plain_frame The complete datagram to protect, header included.
    /// @return The wrapper's length.
    /// @retval kmx::aio::knx::error::invalid_length @p destination is too small, or the datagram exceeds
    ///         @ref max_wrapped_frame_size.
    /// @retval kmx::aio::knx::error::malformed_frame, kmx::aio::knx::error::unsupported_service As for
    ///         @ref check_wrapped_frame.
    /// @retval kmx::aio::knx::error::crypto_failure The backend failed; @p destination is wiped.
    [[nodiscard]] expected_size_t seal_wrapper(span_uint8_t destination, const secret_key& key, const wrapper_fields& fields,
                                               cspan_uint8_t plain_frame) noexcept;

    /// @brief Verifies a SECURE_WRAPPER and decrypts the datagram it carries.
    /// @param destination Receives the plain datagram; must not overlap the wrapper's encrypted frame.
    /// @param key The session key, or the backbone key for routing.
    /// @param value The decoded wrapper.
    /// @return The plain datagram's length.
    /// @retval kmx::aio::knx::error::secure_authentication_failed The MAC does not verify; @p destination is wiped.
    /// @retval kmx::aio::knx::error::invalid_length @p destination is too small.
    /// @retval kmx::aio::knx::error::crypto_failure The backend failed; @p destination is wiped.
    /// @note Authentication only: whether the datagram may be wrapped at all is @ref check_wrapped_frame's question.
    [[nodiscard]] expected_size_t open_wrapper(span_uint8_t destination, const secret_key& key, const wrapper_frame& value) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
