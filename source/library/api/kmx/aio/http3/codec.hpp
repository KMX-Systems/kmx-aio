/// @file aio/http3/codec.hpp
/// @brief HTTP/3 codec definitions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#include <kmx/aio/http3/control.hpp>
#include <kmx/aio/http3/frame.hpp>
#include <kmx/aio/http3/message.hpp>
#include <kmx/aio/http3/qpack.hpp>
#include <kmx/aio/http3/settings.hpp>

#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace kmx::aio::http3
{
    /// @brief HTTP/3 frame envelope encode/decode helpers.
    class frame_codec
    {
    public:
        /// @brief Encodes a frame envelope from a type and payload.
        /// @param type The HTTP/3 frame type.
        /// @param payload The frame payload bytes.
        /// @return Serialized frame envelope bytes.
        static std::vector<std::uint8_t> encode(frame_type type, std::span<const std::uint8_t> payload) noexcept(false);
        /// @brief Decodes a single frame envelope.
        /// @param payload The encoded frame envelope bytes.
        /// @return The decoded frame or a parse error.
        static std::expected<frame, std::error_code> decode(std::span<const std::uint8_t> payload) noexcept;
        /// @brief Decodes all frame envelopes contained in a buffer.
        /// @param payload The encoded byte buffer.
        /// @return All decoded frames or a parse error.
        static std::expected<std::vector<frame>, std::error_code> decode_all(std::span<const std::uint8_t> payload) noexcept;
    };

    /// @brief Explicit HEADERS frame codec built on top of the QPACK layer.
    class headers_codec
    {
    public:
        /// @brief Encodes header fields into a literal header block.
        /// @param headers The header fields to encode.
        /// @return Encoded header block bytes.
        static std::vector<std::uint8_t> encode(const header_list& headers) noexcept(false);
        /// @brief Decodes a literal header block into header fields.
        /// @param payload The encoded header block bytes.
        /// @return The decoded header list or a parse error.
        static std::expected<header_list, std::error_code> decode(std::span<const std::uint8_t> payload) noexcept;
        /// @brief Encodes a HEADERS frame containing the given headers.
        /// @param headers The header fields to encode.
        /// @return Serialized HEADERS frame bytes.
        static std::vector<std::uint8_t> encode_frame(const header_list& headers) noexcept(false);
        /// @brief Decodes a HEADERS frame and returns its header fields.
        /// @param payload The encoded frame bytes.
        /// @return The decoded header list or a parse error.
        static std::expected<header_list, std::error_code> decode_frame(std::span<const std::uint8_t> payload) noexcept;
    };

    /// @brief Explicit DATA frame codec.
    class data_codec
    {
    public:
        /// @brief Copies a DATA payload into owned storage.
        /// @param payload The payload bytes to encode.
        /// @return Serialized DATA payload bytes.
        static std::vector<std::uint8_t> encode(std::span<const std::uint8_t> payload) noexcept(false);
        /// @brief Copies a DATA payload out of owned storage.
        /// @param payload The encoded payload bytes.
        /// @return The decoded payload bytes or a parse error.
        static std::expected<std::vector<std::uint8_t>, std::error_code> decode(std::span<const std::uint8_t> payload) noexcept;
        /// @brief Encodes a DATA frame containing the given payload.
        /// @param payload The payload bytes to encode.
        /// @return Serialized DATA frame bytes.
        static std::vector<std::uint8_t> encode_frame(std::span<const std::uint8_t> payload) noexcept(false);
        /// @brief Decodes a DATA frame and returns its payload.
        /// @param payload The encoded frame bytes.
        /// @return The decoded payload bytes or a parse error.
        static std::expected<std::vector<std::uint8_t>, std::error_code> decode_frame(std::span<const std::uint8_t> payload) noexcept;
    };

    /// @brief Helpers for HTTP/3 SETTINGS payload encoding/decoding.
    class settings_codec
    {
    public:
        /// @brief Encodes HTTP/3 settings into a payload block.
        /// @param value The settings model to encode.
        /// @return Serialized settings payload bytes.
        static std::vector<std::uint8_t> encode(const settings& value) noexcept(false);
        /// @brief Decodes a settings payload block.
        /// @param payload The encoded payload bytes.
        /// @return The decoded settings or a parse error.
        static std::expected<settings, std::error_code> decode(std::span<const std::uint8_t> payload) noexcept;
        /// @brief Encodes a SETTINGS frame containing the given settings.
        /// @param value The settings model to encode.
        /// @return Serialized SETTINGS frame bytes.
        static std::vector<std::uint8_t> encode_frame(const settings& value) noexcept(false);
        /// @brief Decodes a SETTINGS frame and returns the settings payload.
        /// @param payload The encoded frame bytes.
        /// @return The decoded settings or a parse error.
        static std::expected<settings, std::error_code> decode_frame(std::span<const std::uint8_t> payload) noexcept;
    };

    class goaway_codec
    {
    public:
        /// @brief Encodes a GOAWAY payload.
        /// @param value The GOAWAY frame payload to encode.
        /// @return Serialized GOAWAY payload bytes.
        static std::vector<std::uint8_t> encode(const goaway_frame& value) noexcept(false);
        /// @brief Decodes a GOAWAY payload.
        /// @param payload The encoded payload bytes.
        /// @return The decoded GOAWAY payload or a parse error.
        static std::expected<goaway_frame, std::error_code> decode(std::span<const std::uint8_t> payload) noexcept;
        /// @brief Encodes a GOAWAY frame containing the given payload.
        /// @param value The GOAWAY frame payload to encode.
        /// @return Serialized GOAWAY frame bytes.
        static std::vector<std::uint8_t> encode_frame(const goaway_frame& value) noexcept(false);
        /// @brief Decodes a GOAWAY frame and returns its payload.
        /// @param payload The encoded frame bytes.
        /// @return The decoded GOAWAY payload or a parse error.
        static std::expected<goaway_frame, std::error_code> decode_frame(std::span<const std::uint8_t> payload) noexcept;
    };

    class control_stream_codec
    {
    public:
        /// @brief Encodes the opening control stream bytes.
        /// @param value The initial settings to place on the control stream.
        /// @return Serialized control stream bytes.
        static std::vector<std::uint8_t> encode_opening(const settings& value) noexcept(false);
        /// @brief Appends a GOAWAY frame to an existing control stream byte buffer.
        /// @param control_stream_bytes The current control stream bytes.
        /// @param value The GOAWAY payload to append.
        /// @return The updated control stream bytes.
        static std::vector<std::uint8_t> append_goaway(std::span<const std::uint8_t> control_stream_bytes,
                                                       const goaway_frame& value) noexcept(false);
        /// @brief Decodes a complete control stream byte buffer.
        /// @param payload The encoded control stream bytes.
        /// @return The decoded control stream state or a protocol error.
        static std::expected<control_stream_state, std::error_code> decode(std::span<const std::uint8_t> payload) noexcept;
    };
} // namespace kmx::aio::http3

namespace kmx::aio::http3::demo
{
    /// @brief Transitional payload builder for current QUIC HTTP/3 demo
    /// samples.
    /// @details This does not implement full QPACK yet; it provides a stable
    /// framed demo format based on
    ///          HTTP/3 frame envelopes with a simple literal header block
    ///          encoding.
    class message_builder
    {
    public:
        /// @brief Builds a demo request payload in HTTP/0.9-like textual form.
        /// @param request The request metadata.
        /// @param body The request body.
        /// @return Serialized request payload text.
        static std::string make_request_payload(const request_head& request, std::string_view body = {}) noexcept(false);
        /// @brief Builds a demo response payload in HTTP/1.0-like textual form.
        /// @param response The response metadata.
        /// @param body The response body.
        /// @return Serialized response payload text.
        static std::string make_response_payload(const response_head& response, std::string_view body = {}) noexcept(false);
        /// @brief Parses a demo request payload.
        /// @param payload The textual payload to parse.
        /// @return The decoded request or a parse error.
        static std::expected<request_message, std::error_code> parse_request_payload(std::string_view payload) noexcept;
        /// @brief Parses a demo response payload.
        /// @param payload The textual payload to parse.
        /// @return The decoded response or a parse error.
        static std::expected<response_message, std::error_code> parse_response_payload(std::string_view payload) noexcept;
        /// @brief Builds demo request frames for the QUIC HTTP/3 samples.
        /// @param request The request metadata.
        /// @param body The request body.
        /// @return Serialized frame bytes.
        static std::vector<std::uint8_t> make_request_frames(const request_head& request, std::string_view body = {}) noexcept(false);
        /// @brief Builds demo response frames for the QUIC HTTP/3 samples.
        /// @param response The response metadata.
        /// @param body The response body.
        /// @return Serialized frame bytes.
        static std::vector<std::uint8_t> make_response_frames(const response_head& response, std::string_view body = {}) noexcept(false);
        /// @brief Parses demo request frames.
        /// @param payload The encoded frame bytes.
        /// @return The decoded request or a parse error.
        static std::expected<request_message, std::error_code> parse_request_frames(std::span<const std::uint8_t> payload) noexcept;
        /// @brief Parses demo response frames.
        /// @param payload The encoded frame bytes.
        /// @return The decoded response or a parse error.
        static std::expected<response_message, std::error_code> parse_response_frames(std::span<const std::uint8_t> payload) noexcept;
    };
} // namespace kmx::aio::http3::demo