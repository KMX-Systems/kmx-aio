/// @file src/kmx/aio/http3/demo/message_builder.cpp
/// @brief HTTP/3 demo request and response messages, as HTTP/1-style text and as HTTP/3 frames.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/http3/demo/message_builder.hpp>
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/http3/data_codec.hpp>
    #include <kmx/aio/http3/frame.hpp>
    #include <kmx/aio/http3/frame_codec.hpp>
    #include <kmx/aio/http3/headers_codec.hpp>
    #include <kmx/aio/http3/message.hpp>
    #include <kmx/aio/invalid_argument.hpp>

    #include <array>
    #include <charconv>
    #include <cstddef>
    #include <cstdint>
    #include <string>
    #include <string_view>
    #include <utility>
    #include <vector>
#endif

namespace kmx::aio::http3::demo
{
    namespace detail
    {
        [[nodiscard]] std::error_code message_parse_error() noexcept
        {
            return make_error_code(error_code::message_error);
        }

        [[nodiscard]] std::string_view pseudo_to_header_name(const std::string_view pseudo_name) noexcept
        {
            if (pseudo_name.empty() || (pseudo_name.front() != ':'))
                return pseudo_name;

            // The literal is returned rather than the argument so the result outlives the buffer the
            // name was parsed out of. Comparing views costs a length check first, so a name of the
            // wrong length is rejected without looking at a single character.
            static constexpr std::array<std::string_view, 5u> known {":path", ":method", ":scheme", ":status", ":authority"};
            for (const auto name: known)
                if (name == pseudo_name)
                    return name;
            return pseudo_name;
        }

        [[nodiscard]] std::string reason_phrase(const std::uint16_t status)
        {
            switch (status)
            {
                case 200u:
                    return "OK";
                case 400u:
                    return "Bad Request";
                case 404u:
                    return "Not Found";
                case 500u:
                    return "Internal Server Error";
                default:
                    return "Status";
            }
        }

        [[nodiscard]] bool has_header(const header_list& headers, const std::string_view name) noexcept
        {
            for (const auto& [header_name, _]: headers)
                if (header_name == name)
                    return true;

            return false;
        }

        [[nodiscard]] constexpr bool is_ascii_space(const char ch) noexcept
        {
            switch (ch)
            {
                case ' ':
                case '\t':
                case '\r':
                case '\n':
                case '\v':
                case '\f':
                    return true;
                default:
                    return false;
            }
        }

        [[nodiscard]] std::string_view trim(std::string_view text) noexcept
        {
            while (!text.empty() && is_ascii_space(text.front()))
                text.remove_prefix(1u);
            while (!text.empty() && is_ascii_space(text.back()))
                text.remove_suffix(1u);
            return text;
        }

        [[nodiscard]] std::expected<std::pair<std::string_view, std::string_view>, std::error_code> split_head_and_body(
            std::string_view payload) noexcept
        {
            const std::size_t separator = payload.find("\r\n\r\n");
            if (separator == std::string_view::npos)
                return std::unexpected(make_error_code(error_code::message_error));

            return std::pair<std::string_view, std::string_view> {
                payload.substr(0u, separator),
                payload.substr(separator + 4u),
            };
        }

        [[nodiscard]] std::expected<std::pair<std::string_view, std::string_view>, std::error_code> split_header(
            std::string_view line) noexcept
        {
            const std::size_t separator = line.find(':');
            if (separator == std::string_view::npos)
                return std::unexpected(make_error_code(error_code::message_error));

            return std::pair<std::string_view, std::string_view> {
                trim(line.substr(0u, separator)),
                trim(line.substr(separator + 1u)),
            };
        }

        [[nodiscard]] std::vector<std::uint8_t> string_view_to_bytes(const std::string_view text)
        {
            return std::vector<std::uint8_t>(text.begin(), text.end());
        }

        [[nodiscard]] std::string bytes_to_string(cspan_uint8_t bytes)
        {
            return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
    }

    /// @brief Reads the CRLF-terminated header lines of a message head.
    /// @tparam OnHeader A callable taking one header's name and value.
    /// @param head The head section, positioned at its first header line.
    /// @param on_header Called once per header line, in order.
    /// @return Nothing, or why a line is not a header.
    /// @details The run ends at the first empty line or at the end of the head, whichever comes first.
    template <typename OnHeader>
    [[nodiscard]] static std::expected<void, std::error_code> read_header_lines(std::string_view head, OnHeader&& on_header) noexcept
    {
        while (!head.empty())
        {
            const std::size_t line_end = head.find("\r\n");
            const std::string_view line = (line_end == std::string_view::npos) ? head : head.substr(0u, line_end);
            if (line.empty())
                break;

            auto split = detail::split_header(line);
            if (!split)
                return std::unexpected(split.error());
            on_header(split->first, split->second);

            if (line_end == std::string_view::npos)
                break;
            head.remove_prefix(line_end + 2u);
        }

        return {};
    }

    /// @brief Reads a decimal HTTP status code.
    /// @param text The status code's characters, and nothing else.
    /// @return The code, or why the characters are not one.
    [[nodiscard]] static std::expected<std::uint16_t, std::error_code> parse_status(const std::string_view text) noexcept
    {
        std::uint32_t parsed {};
        const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if ((ec != std::errc {}) || (ptr != (text.data() + text.size())) || (parsed > 65535u))
            return std::unexpected(detail::message_parse_error());
        return static_cast<std::uint16_t>(parsed);
    }

    /// @brief Appends a DATA frame's payload to a message body.
    /// @param body The body to append to.
    /// @param payload The frame's payload.
    /// @return Nothing, or why the frame could not be read.
    /// @note Appended straight from the decoded bytes: converting to a string first would build a whole
    ///       second copy of the body only for the append to copy it again and throw it away.
    [[nodiscard]] static std::expected<void, std::error_code> append_body(std::string& body, const cspan_uint8_t payload) noexcept
    {
        auto decoded = data_codec::decode(payload);
        if (!decoded)
            return std::unexpected(decoded.error());

        body.append(reinterpret_cast<const char*>(decoded->data()), decoded->size());
        return {};
    }

    /// @brief Places one decoded header onto a request head, recognising the request pseudo-headers.
    /// @param head The head to fill in.
    /// @param name The header's name.
    /// @param value The header's value.
    static void apply_request_header(request_head& head, const std::string& name, const std::string& value) noexcept(false)
    {
        if (name == ":method")
            head.method = value;
        else if (name == ":scheme")
            head.scheme = value;
        else if (name == ":authority")
            head.authority = value;
        else if (name == ":path")
            head.target = value;
        else
            head.headers.emplace_back(name, value);
    }

    /// @brief Places one decoded header onto a response head, recognising the status pseudo-header.
    /// @param head The head to fill in.
    /// @param name The header's name.
    /// @param value The header's value.
    /// @return Nothing, or why the status could not be read.
    [[nodiscard]] static std::expected<void, std::error_code> apply_response_header(response_head& head, const std::string& name,
                                                                                    const std::string& value) noexcept
    {
        if (name != ":status")
        {
            head.headers.emplace_back(name, value);
            return {};
        }

        const auto status = parse_status(value);
        if (!status.has_value())
            return std::unexpected(status.error());

        head.status = *status;
        return {};
    }

    /// @brief Writes a header list as "name: value" lines.
    /// @param payload The payload to append to.
    /// @param headers The headers to write.
    static void append_header_lines(std::string& payload, const header_list& headers) noexcept(false)
    {
        for (const auto& [name, value]: headers)
        {
            payload += name;
            payload += ": ";
            payload += value;
            payload += "\r\n";
        }
    }

    /// @brief Appends a Content-Length header unless the caller already supplied one.
    /// @param payload The payload to append to.
    /// @param headers The headers already written, searched for an existing Content-Length.
    /// @param body The body whose length is being announced.
    static void append_content_length(std::string& payload, const header_list& headers, const std::string_view body) noexcept(false)
    {
        if (detail::has_header(headers, "Content-Length"))
            return;

        payload += "Content-Length: ";
        payload += std::to_string(body.size());
        payload += "\r\n";
    }

    /// @brief Reads a request line into a request head.
    /// @param line The request line, without its terminator.
    /// @param head The head to fill in.
    /// @return Nothing, or why the line is not a request line.
    /// @note The version that follows the target is accepted and ignored; this demo speaks one version.
    [[nodiscard]] static std::expected<void, std::error_code> parse_request_line(const std::string_view line, request_head& head) noexcept
    {
        const std::size_t first_space = line.find(' ');
        const std::size_t second_space = line.rfind(' ');
        if ((first_space == std::string_view::npos) || (second_space == std::string_view::npos) || (first_space == second_space))
            return std::unexpected(detail::message_parse_error());

        head.method = std::string(line.substr(0u, first_space));
        head.target = std::string(line.substr(first_space + 1u, second_space - first_space - 1u));
        return {};
    }

    /// @brief Reads the status code out of a status line.
    /// @param line The status line, without its terminator.
    /// @return The code, or why the line does not carry one.
    /// @note The version before the code and the reason phrase after it are both ignored.
    [[nodiscard]] static std::expected<std::uint16_t, std::error_code> parse_status_line(const std::string_view line) noexcept
    {
        const std::size_t first_space = line.find(' ');
        if (first_space == std::string_view::npos)
            return std::unexpected(detail::message_parse_error());

        const std::size_t second_space = line.find(' ', first_space + 1u);
        const auto status_text = (second_space == std::string_view::npos) ? line.substr(first_space + 1u) :
                                                                            line.substr(first_space + 1u, second_space - first_space - 1u);
        return parse_status(status_text);
    }

    /// @brief Returns how many octets a header list occupies once written as "name: value\r\n" lines.
    /// @param headers The headers to measure.
    /// @return Their encoded size, so the payload string is allocated once rather than grown.
    [[nodiscard]] static std::size_t headers_size(const header_list& headers) noexcept
    {
        std::size_t total {};
        for (const auto& [name, value]: headers)
            total += name.size() + value.size() + 4u; // ": " and "\r\n"
        return total;
    }

    std::string message_builder::make_request_payload(const request_head& request, std::string_view body) noexcept(false)
    {
        if (request.method.empty())
            throw invalid_argument("HTTP/3 demo request requires a method");
        if (request.target.empty())
            throw invalid_argument("HTTP/3 demo request requires a target");
        if (request.authority.empty())
            throw invalid_argument("HTTP/3 demo request requires an authority");

        std::string payload;
        payload.reserve(96u + request.method.size() + request.target.size() + request.authority.size() + body.size() +
                        headers_size(request.headers));
        payload += request.method;
        payload += ' ';
        payload += request.target;
        payload += " HTTP/0.9\r\n";
        payload += "Host: ";
        payload += request.authority;
        payload += "\r\n";

        append_header_lines(payload, request.headers);

        if (!detail::has_header(request.headers, "Connection"))
            payload += "Connection: close\r\n";

        if (!body.empty())
            append_content_length(payload, request.headers, body);

        payload += "\r\n";
        payload += body;
        return payload;
    }

    std::string message_builder::make_response_payload(const response_head& response, std::string_view body) noexcept(false)
    {
        std::size_t total_size = 96u + body.size();
        for (const auto& [name, value]: response.headers)
            total_size += name.size() + value.size() + 4u; // ": " and "\r\n"

        std::string payload;
        payload.reserve(total_size);
        payload += "HTTP/1.0 ";
        payload += std::to_string(response.status);
        payload += ' ';
        payload += detail::reason_phrase(response.status);
        payload += "\r\n";

        append_header_lines(payload, response.headers);

        if (!detail::has_header(response.headers, "Content-Length"))
        {
            payload += "Content-Length: ";
            payload += std::to_string(body.size());
            payload += "\r\n";
        }

        if (!detail::has_header(response.headers, "Connection"))
            payload += "Connection: close\r\n";

        payload += "\r\n";
        payload += body;
        return payload;
    }

    std::expected<request_message, std::error_code> message_builder::parse_request_payload(const std::string_view payload) noexcept
    {
        auto head_and_body = detail::split_head_and_body(payload);
        if (!head_and_body)
            return std::unexpected(head_and_body.error());

        request_message message {};
        message.body = std::string(head_and_body->second);

        const std::size_t request_line_end = head_and_body->first.find("\r\n");
        if (request_line_end == std::string_view::npos)
            return std::unexpected(detail::message_parse_error());
        if (const auto parsed = parse_request_line(head_and_body->first.substr(0u, request_line_end), message.head); !parsed.has_value())
            return std::unexpected(parsed.error());

        // Host is the HTTP/1 spelling of the :authority pseudo-header, so it is lifted out of the
        // ordinary headers rather than left among them.
        const auto read = read_header_lines(head_and_body->first.substr(request_line_end + 2u),
                                            [&message](const std::string_view name, const std::string_view value)
                                            {
                                                if (name == "Host")
                                                    message.head.authority = std::string(value);
                                                else
                                                    message.head.headers.emplace_back(std::string(name), std::string(value));
                                            });
        if (!read.has_value())
            return std::unexpected(read.error());

        return message;
    }

    std::expected<response_message, std::error_code> message_builder::parse_response_payload(const std::string_view payload) noexcept
    {
        auto head_and_body = detail::split_head_and_body(payload);
        if (!head_and_body)
            return std::unexpected(head_and_body.error());

        response_message message {};
        message.body = std::string(head_and_body->second);

        const std::size_t status_line_end = head_and_body->first.find("\r\n");
        if (status_line_end == std::string_view::npos)
            return std::unexpected(detail::message_parse_error());

        const auto status = parse_status_line(head_and_body->first.substr(0u, status_line_end));
        if (!status.has_value())
            return std::unexpected(status.error());
        message.head.status = *status;

        const auto read = read_header_lines(head_and_body->first.substr(status_line_end + 2u),
                                            [&message](const std::string_view name, const std::string_view value)
                                            { message.head.headers.emplace_back(std::string(name), std::string(value)); });
        if (!read.has_value())
            return std::unexpected(read.error());

        return message;
    }

    std::vector<std::uint8_t> message_builder::make_request_frames(const request_head& request, std::string_view body) noexcept(false)
    {
        header_list headers {
            {":method", request.method},
            {":scheme", request.scheme},
            {":authority", request.authority},
            {":path", request.target},
        };
        headers.insert(headers.end(), request.headers.begin(), request.headers.end());

        auto encoded = headers_codec::encode_frame(headers);

        if (!body.empty())
        {
            const auto data_frame =
                data_codec::encode_frame(cspan_uint8_t(reinterpret_cast<const std::uint8_t*>(body.data()), body.size()));
            encoded.insert(encoded.end(), data_frame.begin(), data_frame.end());
        }

        return encoded;
    }

    std::vector<std::uint8_t> message_builder::make_response_frames(const response_head& response, std::string_view body) noexcept(false)
    {
        header_list headers {
            {":status", std::to_string(response.status)},
        };
        headers.insert(headers.end(), response.headers.begin(), response.headers.end());

        auto encoded = headers_codec::encode_frame(headers);

        if (!body.empty())
        {
            const auto data_frame =
                data_codec::encode_frame(cspan_uint8_t(reinterpret_cast<const std::uint8_t*>(body.data()), body.size()));
            encoded.insert(encoded.end(), data_frame.begin(), data_frame.end());
        }

        return encoded;
    }

    std::expected<request_message, std::error_code> message_builder::parse_request_frames(cspan_uint8_t payload) noexcept
    {
        auto frames = frame_codec::decode_all(payload);
        if (!frames)
            return std::unexpected(frames.error());

        request_message message {};
        bool have_headers {};
        for (const auto& frame: *frames)
            if (frame.type == frame_type::headers)
            {
                auto headers = headers_codec::decode(frame.payload);
                if (!headers)
                    return std::unexpected(headers.error());
                for (const auto& [name, value]: *headers)
                    apply_request_header(message.head, name, value);
                have_headers = true;
            }
            else if (frame.type == frame_type::data)
                if (const auto appended = append_body(message.body, frame.payload); !appended.has_value())
                    return std::unexpected(appended.error());

        if (!have_headers)
            return std::unexpected(detail::message_parse_error());
        return message;
    }

    std::expected<response_message, std::error_code> message_builder::parse_response_frames(cspan_uint8_t payload) noexcept
    {
        auto frames = frame_codec::decode_all(payload);
        if (!frames)
            return std::unexpected(frames.error());

        response_message message {};
        bool have_headers {};
        for (const auto& frame: *frames)
            if (frame.type == frame_type::headers)
            {
                auto headers = headers_codec::decode(frame.payload);
                if (!headers)
                    return std::unexpected(headers.error());
                for (const auto& [name, value]: *headers)
                    if (const auto applied = apply_response_header(message.head, name, value); !applied.has_value())
                        return std::unexpected(applied.error());
                have_headers = true;
            }
            else if (frame.type == frame_type::data)
                if (const auto appended = append_body(message.body, frame.payload); !appended.has_value())
                    return std::unexpected(appended.error());

        if (!have_headers)
            return std::unexpected(detail::message_parse_error());
        return message;
    }
}
