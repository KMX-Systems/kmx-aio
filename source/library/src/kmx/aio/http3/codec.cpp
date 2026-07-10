#include <kmx/aio/http3/codec.hpp>

#include "varint.hpp"

#include <kmx/aio/basic_types.hpp>

#include <cctype>
#include <charconv>
#include <stdexcept>

namespace kmx::aio::http3
{
    namespace detail
    {
        [[nodiscard]] std::error_code frame_parse_error() noexcept
        {
            return make_error_code(error_code::frame_error);
        }

        [[nodiscard]] std::error_code message_parse_error() noexcept
        {
            return make_error_code(error_code::message_error);
        }
    } // namespace detail

    std::vector<std::uint8_t> frame_codec::encode(const frame_type type, std::span<const std::uint8_t> payload) noexcept(false)
    {
        std::vector<std::uint8_t> encoded;
        const std::size_t exact_capacity =
            detail::varint_size(static_cast<std::uint64_t>(type)) + detail::varint_size(payload.size()) + payload.size();
        encoded.reserve(exact_capacity);
        detail::encode_varint(encoded, static_cast<std::uint64_t>(type));
        detail::encode_varint(encoded, payload.size());
        encoded.insert(encoded.end(), payload.begin(), payload.end());
        return encoded;
    }

    std::expected<frame, std::error_code> frame_codec::decode(std::span<const std::uint8_t> payload) noexcept
    {
        auto type_result = detail::decode_varint(payload, 0u);
        if (!type_result)
            return std::unexpected(detail::frame_parse_error());

        auto length_result = detail::decode_varint(payload, type_result->second);
        if (!length_result)
            return std::unexpected(detail::frame_parse_error());

        const std::size_t payload_offset = type_result->second + length_result->second;
        if (payload_offset + length_result->first > payload.size())
            return std::unexpected(detail::frame_parse_error());

        frame decoded {};
        decoded.type = static_cast<frame_type>(type_result->first);
        decoded.payload.assign(payload.begin() + static_cast<std::ptrdiff_t>(payload_offset),
                               payload.begin() + static_cast<std::ptrdiff_t>(payload_offset + length_result->first));
        return decoded;
    }

    std::expected<std::vector<frame>, std::error_code> frame_codec::decode_all(std::span<const std::uint8_t> payload) noexcept
    {
        std::vector<frame> frames;
        frames.reserve(2u);
        std::size_t offset {};
        while (offset < payload.size())
        {
            auto type_result = detail::decode_varint(payload, offset);
            if (!type_result)
                return std::unexpected(detail::frame_parse_error());

            auto length_result = detail::decode_varint(payload, offset + type_result->second);
            if (!length_result)
                return std::unexpected(detail::frame_parse_error());

            const std::size_t payload_offset = offset + type_result->second + length_result->second;
            if (payload_offset + length_result->first > payload.size())
                return std::unexpected(detail::frame_parse_error());

            frame decoded {};
            decoded.type = static_cast<frame_type>(type_result->first);
            decoded.payload.assign(payload.begin() + static_cast<std::ptrdiff_t>(payload_offset),
                                   payload.begin() + static_cast<std::ptrdiff_t>(payload_offset + length_result->first));
            frames.push_back(std::move(decoded));
            offset = payload_offset + static_cast<std::size_t>(length_result->first);
        }

        return frames;
    }

    std::vector<std::uint8_t> headers_codec::encode(const header_list& headers) noexcept(false)
    {
        return qpack::literal_codec::encode(headers);
    }

    std::expected<header_list, std::error_code> headers_codec::decode(std::span<const std::uint8_t> payload) noexcept
    {
        return qpack::literal_codec::decode(payload);
    }

    std::vector<std::uint8_t> headers_codec::encode_frame(const header_list& headers) noexcept(false)
    {
        const auto block = encode(headers);
        return frame_codec::encode(frame_type::headers, block);
    }

    std::expected<header_list, std::error_code> headers_codec::decode_frame(std::span<const std::uint8_t> payload) noexcept
    {
        auto decoded = frame_codec::decode(payload);
        if (!decoded)
            return std::unexpected(decoded.error());
        if (decoded->type != frame_type::headers)
            return std::unexpected(kmx::aio::error_from_errno(EINVAL));
        return decode(decoded->payload);
    }

    std::vector<std::uint8_t> data_codec::encode(std::span<const std::uint8_t> payload) noexcept(false)
    {
        return std::vector<std::uint8_t>(payload.begin(), payload.end());
    }

    std::expected<std::vector<std::uint8_t>, std::error_code> data_codec::decode(std::span<const std::uint8_t> payload) noexcept
    {
        return std::vector<std::uint8_t>(payload.begin(), payload.end());
    }

    std::vector<std::uint8_t> data_codec::encode_frame(std::span<const std::uint8_t> payload) noexcept(false)
    {
        const auto body = encode(payload);
        return frame_codec::encode(frame_type::data, body);
    }

    std::expected<std::vector<std::uint8_t>, std::error_code> data_codec::decode_frame(std::span<const std::uint8_t> payload) noexcept
    {
        auto decoded = frame_codec::decode(payload);
        if (!decoded)
            return std::unexpected(decoded.error());
        if (decoded->type != frame_type::data)
            return std::unexpected(kmx::aio::error_from_errno(EINVAL));
        return decode(decoded->payload);
    }

    std::vector<std::uint8_t> settings_codec::encode(const settings& value) noexcept(false)
    {
        std::vector<std::uint8_t> payload;
        payload.reserve(32u);

        detail::encode_varint(payload, static_cast<std::uint64_t>(settings_identifier::qpack_max_table_capacity));
        detail::encode_varint(payload, value.qpack_max_table_capacity);

        detail::encode_varint(payload, static_cast<std::uint64_t>(settings_identifier::max_field_section_size));
        detail::encode_varint(payload, value.max_field_section_size);

        detail::encode_varint(payload, static_cast<std::uint64_t>(settings_identifier::qpack_blocked_streams));
        detail::encode_varint(payload, value.qpack_blocked_streams);

        if (value.enable_connect_protocol)
        {
            detail::encode_varint(payload, static_cast<std::uint64_t>(settings_identifier::enable_connect_protocol));
            detail::encode_varint(payload, 1u);
        }

        if (value.h3_datagram)
        {
            detail::encode_varint(payload, static_cast<std::uint64_t>(settings_identifier::h3_datagram));
            detail::encode_varint(payload, 1u);
        }

        return payload;
    }

    std::vector<std::uint8_t> settings_codec::encode_frame(const settings& value) noexcept(false)
    {
        const auto payload = encode(value);
        return frame_codec::encode(frame_type::settings, payload);
    }

    std::expected<settings, std::error_code> settings_codec::decode(std::span<const std::uint8_t> payload) noexcept
    {
        settings parsed {};
        std::size_t offset {};
        while (offset < payload.size())
        {
            auto identifier = detail::decode_varint(payload, offset);
            if (!identifier)
                return std::unexpected(make_error_code(error_code::settings_error));
            offset += identifier->second;

            auto value = detail::decode_varint(payload, offset);
            if (!value)
                return std::unexpected(make_error_code(error_code::settings_error));
            offset += value->second;

            switch (static_cast<settings_identifier>(identifier->first))
            {
                case settings_identifier::qpack_max_table_capacity:
                    parsed.qpack_max_table_capacity = value->first;
                    break;
                case settings_identifier::max_field_section_size:
                    parsed.max_field_section_size = value->first;
                    break;
                case settings_identifier::qpack_blocked_streams:
                    parsed.qpack_blocked_streams = value->first;
                    break;
                case settings_identifier::enable_connect_protocol:
                    parsed.enable_connect_protocol = value->first != 0u;
                    break;
                case settings_identifier::h3_datagram:
                    parsed.h3_datagram = value->first != 0u;
                    break;
                default:
                    break;
            }
        }

        return parsed;
    }

    std::expected<settings, std::error_code> settings_codec::decode_frame(std::span<const std::uint8_t> payload) noexcept
    {
        auto decoded = frame_codec::decode(payload);
        if (!decoded)
            return std::unexpected(decoded.error());
        if (decoded->type != frame_type::settings)
            return std::unexpected(make_error_code(error_code::frame_unexpected));
        return decode(decoded->payload);
    }

    std::vector<std::uint8_t> goaway_codec::encode(const goaway_frame& value) noexcept(false)
    {
        std::vector<std::uint8_t> payload;
        detail::encode_varint(payload, value.stream_id);
        return payload;
    }

    std::expected<goaway_frame, std::error_code> goaway_codec::decode(std::span<const std::uint8_t> payload) noexcept
    {
        auto decoded = detail::decode_varint(payload, 0u);
        if (!decoded)
            return std::unexpected(make_error_code(error_code::id_error));
        if (decoded->second != payload.size())
            return std::unexpected(make_error_code(error_code::id_error));
        return goaway_frame {.stream_id = decoded->first};
    }

    std::vector<std::uint8_t> goaway_codec::encode_frame(const goaway_frame& value) noexcept(false)
    {
        const auto payload = encode(value);
        return frame_codec::encode(frame_type::goaway, payload);
    }

    std::expected<goaway_frame, std::error_code> goaway_codec::decode_frame(std::span<const std::uint8_t> payload) noexcept
    {
        auto decoded = frame_codec::decode(payload);
        if (!decoded)
            return std::unexpected(decoded.error());
        if (decoded->type != frame_type::goaway)
            return std::unexpected(make_error_code(error_code::frame_unexpected));
        return decode(decoded->payload);
    }

    std::vector<std::uint8_t> control_stream_codec::encode_opening(const settings& value) noexcept(false)
    {
        std::vector<std::uint8_t> bytes;
        detail::encode_varint(bytes, static_cast<std::uint64_t>(stream_type::control));
        const auto settings_frame = settings_codec::encode_frame(value);
        bytes.insert(bytes.end(), settings_frame.begin(), settings_frame.end());
        return bytes;
    }

    std::vector<std::uint8_t> control_stream_codec::append_goaway(std::span<const std::uint8_t> control_stream_bytes,
                                                                  const goaway_frame& value) noexcept(false)
    {
        std::vector<std::uint8_t> bytes(control_stream_bytes.begin(), control_stream_bytes.end());
        const auto goaway = goaway_codec::encode_frame(value);
        bytes.insert(bytes.end(), goaway.begin(), goaway.end());
        return bytes;
    }

    std::expected<control_stream_state, std::error_code> control_stream_codec::decode(std::span<const std::uint8_t> payload) noexcept
    {
        auto stream_type_value = detail::decode_varint(payload, 0u);
        if (!stream_type_value)
            return std::unexpected(stream_type_value.error());
        if (static_cast<stream_type>(stream_type_value->first) != stream_type::control)
            return std::unexpected(make_error_code(error_code::stream_creation_error));

        control_stream_state state {};
        const auto frames = frame_codec::decode_all(payload.subspan(stream_type_value->second));
        if (!frames)
            return std::unexpected(frames.error());

        bool first_frame = true;

        for (const auto& frame: *frames)
        {
            if (first_frame)
            {
                first_frame = false;
                if (frame.type != frame_type::settings)
                    return std::unexpected(make_error_code(error_code::missing_settings));
            }

            switch (frame.type)
            {
                case frame_type::settings:
                {
                    if (state.saw_settings)
                        return std::unexpected(make_error_code(error_code::settings_error));

                    auto settings = settings_codec::decode(frame.payload);
                    if (!settings)
                        return std::unexpected(settings.error());
                    state.saw_settings = true;
                    state.negotiated_settings = *settings;
                    break;
                }

                case frame_type::goaway:
                {
                    auto goaway = goaway_codec::decode(frame.payload);
                    if (!goaway)
                        return std::unexpected(goaway.error());
                    state.goaway = *goaway;
                    break;
                }

                case frame_type::data:
                case frame_type::headers:
                case frame_type::push_promise:
                    return std::unexpected(make_error_code(error_code::frame_unexpected));

                case frame_type::cancel_push:
                case frame_type::max_push_id:
                    // Accepted control frames not modeled in control_stream_state
                    // yet.
                    break;

                default:
                    break;
            }
        }

        if (!state.saw_settings)
            return std::unexpected(make_error_code(error_code::missing_settings));

        return state;
    }
} // namespace kmx::aio::http3

namespace kmx::aio::http3::demo
{
    namespace detail
    {
        [[nodiscard]] std::string_view pseudo_to_header_name(const std::string_view pseudo_name) noexcept
        {
            if (pseudo_name.empty() || pseudo_name.front() != ':')
                return pseudo_name;

            const char* const p = pseudo_name.data();
            switch (pseudo_name.size())
            {
                case 5u: // ":path"
                    if ((p[1] == 'p') && (p[2] == 'a') && (p[3] == 't') && (p[4] == 'h'))
                        return ":path";
                    break;

                case 7u: // ":method" / ":scheme"
                    switch (p[1])
                    {
                        case 'm':
                            if ((p[2] == 'e') && (p[3] == 't') && (p[4] == 'h') && (p[5] == 'o') && (p[6] == 'd'))
                                return ":method";
                            break;

                        case 's':
                            if ((p[2] == 'c') && (p[3] == 'h') && (p[4] == 'e') && (p[5] == 'm') && (p[6] == 'e'))
                                return ":scheme";
                            break;

                        default:
                            break;
                    }
                    break;

                case 10u: // ":authority" / ":status"
                    switch (p[1])
                    {
                        case 'a':
                            if ((p[2] == 'u') && (p[3] == 't') && (p[4] == 'h') && (p[5] == 'o') && (p[6] == 'r') &&
                                (p[7] == 'i') && (p[8] == 't') && (p[9] == 'y'))
                                return ":authority";
                            break;

                        case 's':
                            if ((p[2] == 't') && (p[3] == 'a') && (p[4] == 't') && (p[5] == 'u') && (p[6] == 's'))
                                return ":status";
                            break;

                        default:
                            break;
                    }
                    break;

                default:
                    break;
            }

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
            {
                if (header_name == name)
                    return true;
            }

            return false;
        }

        [[nodiscard]] inline constexpr bool is_ascii_space(const char ch) noexcept
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

        std::vector<std::uint8_t> string_view_to_bytes(const std::string_view text)
        {
            return std::vector<std::uint8_t>(text.begin(), text.end());
        }

        std::string bytes_to_string(std::span<const std::uint8_t> bytes)
        {
            return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
    } // namespace detail

    std::string message_builder::make_request_payload(const request_head& request, std::string_view body) noexcept(false)
    {
        if (request.method.empty())
            throw std::invalid_argument("HTTP/3 demo request requires a method");
        if (request.target.empty())
            throw std::invalid_argument("HTTP/3 demo request requires a target");
        if (request.authority.empty())
            throw std::invalid_argument("HTTP/3 demo request requires an authority");

        std::size_t total_size = 96u + request.method.size() + request.target.size() + request.authority.size() + body.size();
        for (const auto& [name, value]: request.headers)
            total_size += name.size() + value.size() + 4u; // ": " and "\r\n"

        std::string payload;
        payload.reserve(total_size);
        payload += request.method;
        payload += ' ';
        payload += request.target;
        payload += " HTTP/0.9\r\n";
        payload += "Host: ";
        payload += request.authority;
        payload += "\r\n";

        for (const auto& [name, value]: request.headers)
        {
            payload += name;
            payload += ": ";
            payload += value;
            payload += "\r\n";
        }

        if (!detail::has_header(request.headers, "Connection"))
            payload += "Connection: close\r\n";

        if (!body.empty() && !detail::has_header(request.headers, "Content-Length"))
        {
            payload += "Content-Length: ";
            payload += std::to_string(body.size());
            payload += "\r\n";
        }

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

        for (const auto& [name, value]: response.headers)
        {
            payload += name;
            payload += ": ";
            payload += value;
            payload += "\r\n";
        }

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
            return std::unexpected(::kmx::aio::http3::detail::message_parse_error());

        std::string_view request_line = head_and_body->first.substr(0u, request_line_end);
        const std::size_t first_space = request_line.find(' ');
        const std::size_t second_space = request_line.rfind(' ');
        if (first_space == std::string_view::npos || second_space == std::string_view::npos || first_space == second_space)
            return std::unexpected(::kmx::aio::http3::detail::message_parse_error());

        message.head.method = std::string(request_line.substr(0u, first_space));
        message.head.target = std::string(request_line.substr(first_space + 1u, second_space - first_space - 1u));

        std::string_view remaining = head_and_body->first.substr(request_line_end + 2u);
        while (!remaining.empty())
        {
            const std::size_t line_end = remaining.find("\r\n");
            const std::string_view line = line_end == std::string_view::npos ? remaining : remaining.substr(0u, line_end);
            if (line.empty())
                break;

            auto split = detail::split_header(line);
            if (!split)
                return std::unexpected(split.error());

            if (split->first == "Host")
                message.head.authority = std::string(split->second);
            else
                message.head.headers.emplace_back(std::string(split->first), std::string(split->second));

            if (line_end == std::string_view::npos)
                break;
            remaining.remove_prefix(line_end + 2u);
        }

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
            return std::unexpected(::kmx::aio::http3::detail::message_parse_error());

        const std::string_view status_line = head_and_body->first.substr(0u, status_line_end);
        const std::size_t first_space = status_line.find(' ');
        const std::size_t second_space = status_line.find(' ', first_space == std::string_view::npos ? 0u : first_space + 1u);
        if (first_space == std::string_view::npos)
            return std::unexpected(::kmx::aio::http3::detail::message_parse_error());

        const std::string_view status_text = second_space == std::string_view::npos ?
                                                 status_line.substr(first_space + 1u) :
                                                 status_line.substr(first_space + 1u, second_space - first_space - 1u);
        std::uint32_t parsed_status {};
        const auto [ptr, ec] = std::from_chars(status_text.data(), status_text.data() + status_text.size(), parsed_status);
        if (ec != std::errc {} || ptr != status_text.data() + status_text.size() || parsed_status > 65535u)
            return std::unexpected(::kmx::aio::http3::detail::message_parse_error());
        message.head.status = static_cast<std::uint16_t>(parsed_status);

        std::string_view remaining = head_and_body->first.substr(status_line_end + 2u);
        while (!remaining.empty())
        {
            const std::size_t line_end = remaining.find("\r\n");
            const std::string_view line = line_end == std::string_view::npos ? remaining : remaining.substr(0u, line_end);
            if (line.empty())
                break;

            auto split = detail::split_header(line);
            if (!split)
                return std::unexpected(split.error());

            message.head.headers.emplace_back(std::string(split->first), std::string(split->second));

            if (line_end == std::string_view::npos)
                break;
            remaining.remove_prefix(line_end + 2u);
        }

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
                data_codec::encode_frame(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(body.data()), body.size()));
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
                data_codec::encode_frame(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(body.data()), body.size()));
            encoded.insert(encoded.end(), data_frame.begin(), data_frame.end());
        }

        return encoded;
    }

    std::expected<request_message, std::error_code> message_builder::parse_request_frames(std::span<const std::uint8_t> payload) noexcept
    {
        auto frames = frame_codec::decode_all(payload);
        if (!frames)
            return std::unexpected(frames.error());

        request_message message {};
        bool have_headers = false;
        for (const auto& frame: *frames)
        {
            if (frame.type == frame_type::headers)
            {
                auto headers = headers_codec::decode(frame.payload);
                if (!headers)
                    return std::unexpected(headers.error());
                for (const auto& [name, value]: *headers)
                {
                    if (name == ":method")
                        message.head.method = value;
                    else if (name == ":scheme")
                        message.head.scheme = value;
                    else if (name == ":authority")
                        message.head.authority = value;
                    else if (name == ":path")
                        message.head.target = value;
                    else
                        message.head.headers.emplace_back(name, value);
                }
                have_headers = true;
            }
            else if (frame.type == frame_type::data)
            {
                auto body = data_codec::decode(frame.payload);
                if (!body)
                    return std::unexpected(body.error());
                message.body += detail::bytes_to_string(*body);
            }
        }

        if (!have_headers)
            return std::unexpected(::kmx::aio::http3::detail::message_parse_error());
        return message;
    }

    std::expected<response_message, std::error_code> message_builder::parse_response_frames(std::span<const std::uint8_t> payload) noexcept
    {
        auto frames = frame_codec::decode_all(payload);
        if (!frames)
            return std::unexpected(frames.error());

        response_message message {};
        bool have_headers = false;
        for (const auto& frame: *frames)
        {
            if (frame.type == frame_type::headers)
            {
                auto headers = headers_codec::decode(frame.payload);
                if (!headers)
                    return std::unexpected(headers.error());
                for (const auto& [name, value]: *headers)
                {
                    if (name == ":status")
                    {
                        std::uint32_t parsed_status {};
                        const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed_status);
                        if (ec != std::errc {} || ptr != value.data() + value.size() || parsed_status > 65535u)
                            return std::unexpected(::kmx::aio::http3::detail::message_parse_error());
                        message.head.status = static_cast<std::uint16_t>(parsed_status);
                    }
                    else
                        message.head.headers.emplace_back(name, value);
                }
                have_headers = true;
            }
            else if (frame.type == frame_type::data)
            {
                auto body = data_codec::decode(frame.payload);
                if (!body)
                    return std::unexpected(body.error());
                message.body += detail::bytes_to_string(*body);
            }
        }

        if (!have_headers)
            return std::unexpected(::kmx::aio::http3::detail::message_parse_error());
        return message;
    }
} // namespace kmx::aio::http3::demo