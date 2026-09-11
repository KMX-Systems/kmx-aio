/// @file src/kmx/aio/knx/secure/detail/xml_parser.cpp
/// @brief The compiled body of the keyring XML parser: the document structure, names, attribute values and references.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/detail/xml_parser.hpp>
#ifndef PCH
    #include <kmx/aio/knx/error.hpp>

    #include <algorithm>
    #include <array>
    #include <charconv>
    #include <cstdint>
    #include <utility>
#endif

namespace kmx::aio::knx::secure::detail
{
    /// @brief The one refusal every structural problem reports.
    [[nodiscard]] static std::unexpected<std::error_code> malformed() noexcept
    {
        return std::unexpected(make_error_code(error::malformed_frame));
    }

    [[nodiscard]] static constexpr bool is_space(const char value) noexcept
    {
        return (value == ' ') || (value == '\t') || (value == '\r') || (value == '\n');
    }

    [[nodiscard]] static constexpr bool is_name_start(const char value) noexcept
    {
        return ((value >= 'A') && (value <= 'Z')) || ((value >= 'a') && (value <= 'z')) || (value == '_') || (value == ':');
    }

    [[nodiscard]] static constexpr bool is_name_char(const char value) noexcept
    {
        return is_name_start(value) || ((value >= '0') && (value <= '9')) || (value == '-') || (value == '.');
    }

    /// @brief Indicates whether a code point is one XML allows in a document.
    [[nodiscard]] static constexpr bool allowed_code_point(const std::uint32_t value) noexcept
    {
        return (value == 0x9u) || (value == 0xAu) || (value == 0xDu) || ((value >= 0x20u) && (value <= 0xD7FFu)) ||
               ((value >= 0xE000u) && (value <= 0xFFFDu)) || ((value >= 0x10000u) && (value <= 0x10FFFFu));
    }

    /// @brief Appends a code point encoded as UTF-8.
    static void append_utf8(std::string& text, const std::uint32_t value) noexcept(false)
    {
        if (value < 0x80u)
            text.push_back(static_cast<char>(value));
        else if (value < 0x800u)
            text.append({static_cast<char>(0xC0u | (value >> 6u)), static_cast<char>(0x80u | (value & 0x3Fu))});
        else if (value < 0x10000u)
            text.append({static_cast<char>(0xE0u | (value >> 12u)), static_cast<char>(0x80u | ((value >> 6u) & 0x3Fu)),
                         static_cast<char>(0x80u | (value & 0x3Fu))});
        else
            text.append({static_cast<char>(0xF0u | (value >> 18u)), static_cast<char>(0x80u | ((value >> 12u) & 0x3Fu)),
                         static_cast<char>(0x80u | ((value >> 6u) & 0x3Fu)), static_cast<char>(0x80u | (value & 0x3Fu))});
    }

    /// @brief Decodes a numeric character reference, `&#65;` or `&#x41;`, without its `&#` and `;`.
    [[nodiscard]] static xml_status_t append_character_reference(std::string& value, const std::string_view digits) noexcept(false)
    {
        const auto hexadecimal = digits.starts_with('x');
        const auto text = hexadecimal ? digits.substr(1u) : digits;
        std::uint32_t code_point {};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), code_point, hexadecimal ? 16 : 10);
        if (text.empty() || (parsed.ec != std::errc {}) || (parsed.ptr != (text.data() + text.size())) || !allowed_code_point(code_point))
            return malformed();
        append_utf8(value, code_point);
        return {};
    }

    xml_events_result_t xml_parser::run() noexcept(false)
    {
        if (text_.empty() || (text_.size() > limits_.max_document_size))
            return std::unexpected(make_error_code(error::invalid_length));
        if (looking_at("\xEF\xBB\xBF"))
            cursor_ += 3u;
        if (const auto declared = skip_declaration(); !declared.has_value())
            return std::unexpected(declared.error());
        if (const auto skipped = skip_misc(); !skipped.has_value())
            return std::unexpected(skipped.error());
        if (at_end() || (text_[cursor_] != '<'))
            return malformed();
        if (const auto root = read_start_tag(); !root.has_value())
            return std::unexpected(root.error());
        if (const auto content = read_content(); !content.has_value())
            return std::unexpected(content.error());
        // One root and nothing after it but whitespace and comments.
        if (const auto trailing = skip_misc(); !trailing.has_value() || !at_end())
            return malformed();
        return std::move(events_);
    }

    void xml_parser::skip_spaces() noexcept
    {
        while (!at_end() && is_space(text_[cursor_]))
            ++cursor_;
    }

    xml_status_t xml_parser::skip_declaration() noexcept
    {
        if (!looking_at("<?xml"))
            return {};
        // "<?xml-stylesheet" and its kind are processing instructions, not the declaration, and are refused.
        const auto after = cursor_ + 5u;
        if ((after < text_.size()) && !is_space(text_[after]) && (text_[after] != '?'))
            return malformed();
        const auto end = text_.find("?>", after);
        if (end == std::string_view::npos)
            return malformed();
        cursor_ = end + 2u;
        return {};
    }

    xml_status_t xml_parser::skip_misc() noexcept
    {
        for (;;)
        {
            skip_spaces();
            if (!looking_at("<!--"))
                return {};
            const auto end = text_.find("-->", cursor_ + 4u);
            if (end == std::string_view::npos)
                return malformed();
            cursor_ = end + 3u;
        }
    }

    xml_status_t xml_parser::read_content() noexcept(false)
    {
        while (!open_.empty())
        {
            if (const auto skipped = skip_misc(); !skipped.has_value())
                return skipped;
            // Anything but a tag here is text content, a DOCTYPE, a CDATA section or a processing instruction -
            // none of which a keyring contains, and each of which is refused rather than skipped.
            if (at_end() || (text_[cursor_] != '<') || looking_at("<!") || looking_at("<?"))
                return malformed();
            const auto read = looking_at("</") ? read_end_tag() : read_start_tag();
            if (!read.has_value())
                return read;
        }

        return {};
    }

    xml_status_t xml_parser::read_start_tag() noexcept(false)
    {
        ++cursor_;
        auto name = read_name();
        if (!name.has_value())
            return std::unexpected(name.error());

        xml_event event {xml_event_kind::start, std::move(*name), {}};
        for (;;)
        {
            const auto before = cursor_;
            skip_spaces();
            if (looking_at("/>") || looking_at(">"))
            {
                const auto empty = looking_at("/>");
                cursor_ += empty ? 2u : 1u;
                return finish_start_tag(std::move(event), empty);
            }

            // XML requires whitespace between a name and an attribute, and between attributes.
            if ((cursor_ == before) || at_end())
                return malformed();
            if (const auto read = read_attribute(event); !read.has_value())
                return read;
        }
    }

    xml_status_t xml_parser::finish_start_tag(xml_event&& event, const bool empty) noexcept(false)
    {
        if ((++element_count_ > limits_.max_elements) || ((open_.size() + 1u) > limits_.max_depth))
            return malformed();

        auto name = event.name;
        events_.push_back(std::move(event));
        if (empty)
            events_.push_back(xml_event {xml_event_kind::end, std::move(name), {}});
        else
            open_.push_back(std::move(name));
        return {};
    }

    xml_status_t xml_parser::read_end_tag() noexcept(false)
    {
        cursor_ += 2u;
        auto name = read_name();
        if (!name.has_value())
            return std::unexpected(name.error());
        skip_spaces();
        if (at_end() || (text_[cursor_] != '>') || (*name != open_.back()))
            return malformed();

        ++cursor_;
        open_.pop_back();
        events_.push_back(xml_event {xml_event_kind::end, std::move(*name), {}});
        return {};
    }

    xml_text_result_t xml_parser::read_name() noexcept(false)
    {
        const auto start = cursor_;
        if (at_end() || !is_name_start(text_[cursor_]))
            return malformed();
        while (!at_end() && is_name_char(text_[cursor_]))
            ++cursor_;
        if ((cursor_ - start) > limits_.max_name_size)
            return malformed();
        return std::string {text_.substr(start, cursor_ - start)};
    }

    xml_status_t xml_parser::read_attribute(xml_event& event) noexcept(false)
    {
        auto name = read_name();
        if (!name.has_value())
            return std::unexpected(name.error());
        skip_spaces();
        if (at_end() || (text_[cursor_] != '='))
            return malformed();
        ++cursor_;
        skip_spaces();

        auto value = read_value();
        if (!value.has_value())
            return std::unexpected(value.error());
        const auto duplicate =
            std::ranges::any_of(event.attributes, [&name](const xml_attribute& held) noexcept { return held.name == *name; });
        if (duplicate || (event.attributes.size() >= limits_.max_attributes))
            return malformed();
        event.attributes.push_back(xml_attribute {std::move(*name), std::move(*value)});
        return {};
    }

    xml_text_result_t xml_parser::read_value() noexcept(false)
    {
        if (at_end() || ((text_[cursor_] != '"') && (text_[cursor_] != '\'')))
            return malformed();
        const auto quote = text_[cursor_++];

        std::string value {};
        while (!at_end() && (text_[cursor_] != quote) && (value.size() <= limits_.max_value_size))
        {
            const auto character = text_[cursor_];
            if (character == '<')
                return malformed();
            if (character == '&')
            {
                if (const auto read = read_reference(value); !read.has_value())
                    return std::unexpected(read.error());
                continue;
            }

            // Attribute value normalisation: a tab, a line feed, a carriage return or a CR LF pair reads as one
            // space. A character reference is not normalised, which is why references are handled above.
            if ((character == '\r') && ((cursor_ + 1u) < text_.size()) && (text_[cursor_ + 1u] == '\n'))
                ++cursor_;
            value.push_back(((character == '\t') || (character == '\n') || (character == '\r')) ? ' ' : character);
            ++cursor_;
        }

        if (at_end() || (value.size() > limits_.max_value_size))
            return malformed();
        ++cursor_;
        return value;
    }

    xml_status_t xml_parser::read_reference(std::string& value) noexcept(false)
    {
        static constexpr std::size_t longest_reference = 10u;
        const auto end = text_.find(';', cursor_);
        if ((end == std::string_view::npos) || ((end - cursor_) > longest_reference))
            return malformed();
        const auto name = text_.substr(cursor_ + 1u, end - cursor_ - 1u);
        cursor_ = end + 1u;
        if (name.starts_with('#'))
            return append_character_reference(value, name.substr(1u));

        static constexpr std::array<std::pair<std::string_view, char>, 5u> predefined {{
            {"amp", '&'},
            {"lt", '<'},
            {"gt", '>'},
            {"quot", '"'},
            {"apos", '\''},
        }};
        const auto found = std::ranges::find(predefined, name, &std::pair<std::string_view, char>::first);
        if (found == predefined.end())
            return malformed();
        value.push_back(found->second);
        return {};
    }
}
