/// @file api/kmx/aio/http3/demo/message_builder.hpp
/// @brief HTTP/3 demo message builder used by the QUIC HTTP/3 samples.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_HTTP3)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/http3/message.hpp>

        #include <cstdint>
        #include <expected>
        #include <string>
        #include <string_view>
        #include <system_error>
        #include <vector>
    #endif

/// @brief Transitional request and response formats for the QUIC HTTP/3 demo samples.
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
        [[nodiscard]] static std::expected<request_message, std::error_code> parse_request_payload(const std::string_view payload) noexcept;
        /// @brief Parses a demo response payload.
        /// @param payload The textual payload to parse.
        /// @return The decoded response or a parse error.
        [[nodiscard]] static std::expected<response_message, std::error_code> parse_response_payload(
            const std::string_view payload) noexcept;
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
        [[nodiscard]] static std::expected<request_message, std::error_code> parse_request_frames(cspan_uint8_t payload) noexcept;
        /// @brief Parses demo response frames.
        /// @param payload The encoded frame bytes.
        /// @return The decoded response or a parse error.
        [[nodiscard]] static std::expected<response_message, std::error_code> parse_response_frames(cspan_uint8_t payload) noexcept;
    };
}
#endif // KMX_AIO_FEATURE_HTTP3
