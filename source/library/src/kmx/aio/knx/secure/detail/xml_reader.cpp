/// @file src/kmx/aio/knx/secure/detail/xml_reader.cpp
/// @brief The compiled body of the bounded XML reader for ETS keyring files.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/detail/xml_reader.hpp>
#ifndef PCH
    #include <kmx/aio/knx/secure/detail/xml_parser.hpp>

    #include <algorithm>
#endif

namespace kmx::aio::knx::secure::detail
{
    xml_events_result_t read_xml(const std::string_view document, const xml_limits& limits) noexcept(false)
    {
        return detail::xml_parser {document, limits}.run();
    }

    const std::string* find_attribute(const xml_event& event, const std::string_view name) noexcept
    {
        const auto found = std::ranges::find(event.attributes, name, &xml_attribute::name);
        return (found == event.attributes.end()) ? nullptr : &found->value;
    }
}
