/// @file api/kmx/aio/knx/discovery.hpp
/// @brief KNXnet/IP discovery frame helpers.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// How a client finds the interfaces on a network and asks what they are. SEARCH is a multicast question
/// answered by every server that hears it, each from its own unicast address; DESCRIPTION is the unicast
/// follow-up to one of them.
///
/// The extended search adds parameters that narrow which servers should answer - by MAC address, by
/// programming mode, by service family. A server that predates it simply does not respond, so a client
/// that wants to find everything issues both searches.
/// @reference KNX System Specifications, 03/08/02 "Core", discovery.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/connection.hpp>
        #include <kmx/aio/task.hpp>

        #include <cstddef>
        #include <cstdint>
        #include <expected>
        #include <system_error>
        #include <variant>
        #include <vector>
        #include <sys/socket.h>
    #endif

namespace kmx::aio::knx::discovery
{
    /// @brief Service type of a SEARCH_REQUEST.
    inline constexpr std::uint16_t search_request_service = 0x0201u;
    /// @brief Service type of a SEARCH_RESPONSE.
    inline constexpr std::uint16_t search_response_service = 0x0202u;
    /// @brief Service type of a DESCRIPTION_REQUEST.
    inline constexpr std::uint16_t description_request_service = 0x0203u;
    /// @brief Service type of a DESCRIPTION_RESPONSE.
    inline constexpr std::uint16_t description_response_service = 0x0204u;
    /// @brief SEARCH_REQUEST_EXTENDED, a search narrowed by search request parameters.
    inline constexpr std::uint16_t search_request_extended_service = 0x020Bu;
    /// @brief SEARCH_RESPONSE_EXTENDED, its answer.
    inline constexpr std::uint16_t search_response_extended_service = 0x020Cu;
    /// @brief Body of a SEARCH_REQUEST: the requester's discovery endpoint.
    inline constexpr std::size_t search_request_body_size = connection::hpai_size;
    /// @brief Body of an IPv6 SEARCH_REQUEST.
    inline constexpr std::size_t ipv6_search_request_body_size = connection::ipv6_hpai_size;
    /// @brief Body of a DESCRIPTION_REQUEST: the requester's control endpoint.
    /// @details The server sends its DESCRIPTION_RESPONSE to this endpoint, so a request without it names
    ///          nowhere to answer. A header-only request is six octets and is rejected by every server.
    inline constexpr std::size_t description_request_body_size = connection::hpai_size;

    /// @brief A SEARCH_REQUEST: who is out there, and where to answer.
    struct search_request_frame
    {
        /// @brief Where answering servers are to send their SEARCH_RESPONSE.
        /// @note Sent to the multicast group, but answered by unicast to this endpoint, which is why a
        ///       search names an address at all.
        hpai discovery_endpoint {};
    };

    /// @brief A SEARCH_REQUEST over IPv6.
    struct ipv6_search_request_frame
    {
        /// @brief Where answering servers are to send their SEARCH_RESPONSE.
        ipv6_hpai discovery_endpoint {};
    };

    /// @brief The search request parameter blocks a SEARCH_REQUEST_EXTENDED may carry.
    /// @reference KNX System Specifications, 03/08/02 "Core", search request parameters.
    enum class search_parameter_type : std::uint8_t
    {
        /// @brief Answer only if in programming mode; carries no data.
        programming_mode = 0x01u,
        /// @brief Answer only if the MAC address matches; carries six octets.
        mac_address = 0x02u,
        /// @brief Answer only if the service family and version match; carries two octets.
        service = 0x03u,
        /// @brief Answer with the named description information blocks; carries their type codes.
        request_dibs = 0x04u,
    };

    /// @brief One search request parameter block.
    /// @details On the wire: a structure length, then one octet whose top bit is the mandatory flag and
    ///          whose low seven bits are the type, then the data. The length counts those two octets too.
    struct search_parameter
    {
        /// @brief Whether a server that cannot honour the parameter must stay silent rather than answer.
        bool mandatory = true;
        /// @brief Which parameter this is.
        search_parameter_type type = search_parameter_type::programming_mode;
        /// @brief The parameter data; empty for @ref search_parameter_type::programming_mode.
        byte_buffer_t data {};
    };

    /// @brief Mask of the type bits within a search request parameter's type octet.
    inline constexpr std::uint8_t search_parameter_type_mask = 0x7Fu;
    /// @brief Mask of the mandatory flag within a search request parameter's type octet.
    inline constexpr std::uint8_t search_parameter_mandatory_mask = 0x80u;
    /// @brief Size of the length and type octets a parameter block begins with.
    inline constexpr std::size_t search_parameter_header_size = 2u;

    /// @brief A SEARCH_REQUEST_EXTENDED.
    struct extended_search_request_frame
    {
        /// @brief Where answering servers are to send their SEARCH_RESPONSE_EXTENDED.
        hpai discovery_endpoint {};
        /// @brief The parameters narrowing the search; an empty list searches for everything.
        std::vector<search_parameter> parameters {};
    };

    /// @brief A SEARCH_RESPONSE_EXTENDED, shaped exactly like a SEARCH_RESPONSE.
    struct extended_search_response_frame
    {
        /// @brief Where the answering server can be reached.
        hpai control_endpoint {};
        /// @brief What the server says about itself, as a run of description information blocks.
        byte_buffer_t device_info_blocks {};
    };

    /// @brief A SEARCH_RESPONSE: one server saying where it is and what it is.
    /// @note The description is kept as octets rather than decoded here; @ref kmx::aio::knx::dib::decode_all
    ///       reads it.
    struct search_response_frame
    {
        /// @brief Where the answering server can be reached.
        hpai control_endpoint {};
        /// @brief What the server says about itself, as a run of description information blocks.
        byte_buffer_t device_info_blocks {};
    };

    /// @brief A SEARCH_RESPONSE over IPv6.
    struct ipv6_search_response_frame
    {
        /// @brief Where the answering server can be reached.
        ipv6_hpai control_endpoint {};
        /// @brief What the server says about itself, as a run of description information blocks.
        byte_buffer_t device_info_blocks {};
    };

    /// @brief A DESCRIPTION_REQUEST: asking one known server to describe itself in full.
    struct description_request_frame
    {
        /// @brief Where the server is to send its DESCRIPTION_RESPONSE.
        hpai control_endpoint {};
    };

    /// @brief A DESCRIPTION_RESPONSE: what one server is.
    /// @note Carries no endpoint, unlike a SEARCH_RESPONSE - the requester already knows where it asked.
    struct description_response_frame
    {
        /// @brief What the server says about itself, as a run of description information blocks.
        byte_buffer_t device_info_blocks {};
    };

    /// @brief Any of the three shapes a search answer can arrive in.
    using search_response = std::variant<search_response_frame, ipv6_search_response_frame, extended_search_response_frame>;

    /// @brief A search response, or the error explaining why none was obtained.
    using search_result_t = std::expected<search_response, std::error_code>;
    /// @brief Task yielding a search response or the error that stopped the search.
    using search_task_t = task<search_result_t>;

    /// @brief One answering server, with the address its answer came from.
    /// @details SEARCH is one request to many servers, and each answers from its own unicast address. The
    ///          source is kept beside the response because it is what tells a caller which device replied
    ///          when several do.
    struct discovered_server
    {
        /// @brief The decoded SEARCH_RESPONSE.
        search_response response {};
        /// @brief The address the response arrived from.
        sockaddr_storage source {};
        /// @brief The number of octets @ref source provides.
        ::socklen_t source_length {};
    };

    /// @brief Every server that answered, or the error that stopped the search.
    using search_all_result_t = std::expected<std::vector<discovered_server>, std::error_code>;
    /// @brief Task yielding every answering server.
    using search_all_task_t = task<search_all_result_t>;

    /// @brief A device description, or the error explaining why none was obtained.
    using description_result_t = std::expected<description_response_frame, std::error_code>;
    /// @brief Task yielding a device description.
    using description_task_t = task<description_result_t>;

    /// @brief Encodes a SEARCH_REQUEST.
    /// @param dest The destination octets; must be large enough for the encoded frame.
    /// @param request The request to encode.
    /// @return Nothing, or the reason the frame could not be encoded.
    [[nodiscard]] expected_void_t encode_search_request_packet(span_uint8_t dest, const search_request_frame& request) noexcept;
    /// @brief Decodes a SEARCH_REQUEST.
    /// @param packet The received octets, header included.
    /// @return The decoded frame, or the reason the octets could not be read.
    [[nodiscard]] std::expected<search_request_frame, std::error_code> decode_search_request_packet(cspan_uint8_t packet) noexcept;
    /// @brief Encodes a SEARCH_REQUEST over IPv6.
    /// @param dest The destination octets; must be large enough for the encoded frame.
    /// @param request The request to encode.
    /// @return Nothing, or the reason the frame could not be encoded.
    [[nodiscard]] expected_void_t encode_ipv6_search_request_packet(span_uint8_t dest, const ipv6_search_request_frame& request) noexcept;
    /// @brief Decodes a SEARCH_REQUEST over IPv6.
    /// @param packet The received octets, header included.
    /// @return The decoded frame, or the reason the octets could not be read.
    [[nodiscard]] std::expected<ipv6_search_request_frame, std::error_code> decode_ipv6_search_request_packet(
        cspan_uint8_t packet) noexcept;
    /// @brief Encodes a SEARCH_RESPONSE.
    /// @param dest The destination octets; must be large enough for the encoded frame.
    /// @param response The response to encode; its description blocks are written verbatim.
    /// @return Nothing, or the reason the frame could not be encoded.
    /// @note The blocks are validated as a well-formed run before being written, so a malformed
    ///       description fails here rather than at the peer.
    [[nodiscard]] expected_void_t encode_search_response_packet(span_uint8_t dest, const search_response_frame& response) noexcept;
    /// @brief Decodes a SEARCH_RESPONSE.
    /// @param packet The received octets, header included.
    /// @return The decoded frame, or the reason the octets could not be read.
    /// @note The description blocks are copied out but not interpreted; read them with
    ///       @ref kmx::aio::knx::dib::decode_all.
    [[nodiscard]] std::expected<search_response_frame, std::error_code> decode_search_response_packet(cspan_uint8_t packet) noexcept;
    /// @brief Encodes a SEARCH_RESPONSE over IPv6.
    /// @param dest The destination octets; must be large enough for the encoded frame.
    /// @param response The response to encode.
    /// @return Nothing, or the reason the frame could not be encoded.
    [[nodiscard]] expected_void_t encode_ipv6_search_response_packet(span_uint8_t dest,
                                                                     const ipv6_search_response_frame& response) noexcept;
    /// @brief Decodes a SEARCH_RESPONSE over IPv6.
    /// @param packet The received octets, header included.
    /// @return The decoded frame, or the reason the octets could not be read.
    [[nodiscard]] std::expected<ipv6_search_response_frame, std::error_code> decode_ipv6_search_response_packet(
        cspan_uint8_t packet) noexcept;
    /// @brief Encodes a DESCRIPTION_REQUEST.
    /// @param dest The destination octets; must be large enough for the encoded frame.
    /// @param request The request to encode.
    /// @return Nothing, or the reason the frame could not be encoded.
    [[nodiscard]] expected_void_t encode_description_request_packet(span_uint8_t dest, const description_request_frame& request) noexcept;
    /// @brief Returns the number of octets a SEARCH_REQUEST_EXTENDED occupies.
    /// @param request The request to measure.
    /// @return The encoded size, or the reason the request could not be measured.
    /// @note Call this before @ref encode_extended_search_request_packet; the parameter blocks make the
    ///       frame variable-length, so its size is not a constant.
    [[nodiscard]] std::expected<std::size_t, std::error_code> extended_search_request_size(
        const extended_search_request_frame& request) noexcept;
    /// @brief Encodes a SEARCH_REQUEST_EXTENDED.
    /// @param dest The destination octets; must be large enough for the encoded frame.
    /// @param request The request to encode, including its parameters.
    /// @return Nothing, or the reason the frame could not be encoded.
    /// @note Size the destination with @ref extended_search_request_size; the parameters make the
    ///       frame variable-length.
    [[nodiscard]] expected_void_t encode_extended_search_request_packet(span_uint8_t dest,
                                                                        const extended_search_request_frame& request) noexcept;
    /// @brief Decodes a SEARCH_REQUEST_EXTENDED, parameters included.
    /// @param packet The received octets, header included.
    /// @return The decoded frame, or the reason the octets could not be read.
    [[nodiscard]] std::expected<extended_search_request_frame, std::error_code> decode_extended_search_request_packet(
        cspan_uint8_t packet) noexcept;
    /// @brief Encodes a SEARCH_RESPONSE_EXTENDED.
    /// @param dest The destination octets; must be large enough for the encoded frame.
    /// @param response The response to encode.
    /// @return Nothing, or the reason the frame could not be encoded.
    [[nodiscard]] expected_void_t encode_extended_search_response_packet(span_uint8_t dest,
                                                                         const extended_search_response_frame& response) noexcept;
    /// @brief Decodes a SEARCH_RESPONSE_EXTENDED.
    /// @param packet The received octets, header included.
    /// @return The decoded frame, or the reason the octets could not be read.
    [[nodiscard]] std::expected<extended_search_response_frame, std::error_code> decode_extended_search_response_packet(
        cspan_uint8_t packet) noexcept;
    /// @brief Decodes a DESCRIPTION_REQUEST.
    /// @param packet The received octets, header included.
    /// @return The decoded frame, or the reason the octets could not be read.
    [[nodiscard]] std::expected<description_request_frame, std::error_code> decode_description_request_packet(
        cspan_uint8_t packet) noexcept;
    /// @brief Encodes a DESCRIPTION_RESPONSE.
    /// @param dest The destination octets; must be large enough for the encoded frame.
    /// @param response The response to encode.
    /// @return Nothing, or the reason the frame could not be encoded.
    [[nodiscard]] expected_void_t encode_description_response_packet(span_uint8_t dest,
                                                                     const description_response_frame& response) noexcept;
    /// @brief Decodes a DESCRIPTION_RESPONSE.
    /// @param packet The received octets, header included.
    /// @return The decoded frame, or the reason the octets could not be read.
    [[nodiscard]] std::expected<description_response_frame, std::error_code> decode_description_response_packet(
        cspan_uint8_t packet) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
