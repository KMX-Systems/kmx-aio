/// @file inc/kmx/aio/knx/secure/detail/xml_parser.hpp
/// @brief The parser behind the bounded keyring XML reader: one document, read within its limits into element events.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/knx/secure/detail/xml_reader.hpp>

        #include <algorithm>
        #include <cstddef>
        #include <expected>
        #include <string>
        #include <string_view>
        #include <system_error>
        #include <vector>
    #endif

namespace kmx::aio::knx::secure::detail
{
    /// @brief Nothing, or why the document was refused.
    using xml_status_t = std::expected<void, std::error_code>;
    /// @brief A piece of decoded text, or why the document was refused.
    using xml_text_result_t = std::expected<std::string, std::error_code>;

    /// @brief Reads one document within its limits, collecting element events.
    class xml_parser final
    {
    public:
        xml_parser(const std::string_view text, const xml_limits& limits) noexcept: text_(text), limits_(limits) {}

        [[nodiscard]] xml_events_result_t run() noexcept(false);

    private:
        [[nodiscard]] bool at_end() const noexcept { return cursor_ >= text_.size(); }
        [[nodiscard]] bool looking_at(const std::string_view prefix) const noexcept
        {
            return text_.substr(std::min(cursor_, text_.size())).starts_with(prefix);
        }
        void skip_spaces() noexcept;

        [[nodiscard]] xml_status_t skip_declaration() noexcept;
        [[nodiscard]] xml_status_t skip_misc() noexcept;
        [[nodiscard]] xml_status_t read_content() noexcept(false);
        [[nodiscard]] xml_status_t read_start_tag() noexcept(false);
        [[nodiscard]] xml_status_t finish_start_tag(xml_event&& event, bool empty) noexcept(false);
        [[nodiscard]] xml_status_t read_end_tag() noexcept(false);
        [[nodiscard]] xml_text_result_t read_name() noexcept(false);
        [[nodiscard]] xml_status_t read_attribute(xml_event& event) noexcept(false);
        [[nodiscard]] xml_text_result_t read_value() noexcept(false);
        [[nodiscard]] xml_status_t read_reference(std::string& value) noexcept(false);

        std::string_view text_;
        xml_limits limits_;
        std::size_t cursor_ {};
        std::size_t element_count_ {};
        xml_events_t events_ {};
        std::vector<std::string> open_ {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
