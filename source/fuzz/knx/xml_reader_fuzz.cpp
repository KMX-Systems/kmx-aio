/// @file fuzz/knx/xml_reader_fuzz.cpp
/// @brief libFuzzer target for the bounded XML reader behind the ETS keyring loader.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details Beyond surviving every input, a document the reader accepts must keep the reader's promises: its
///          events balance, and no element, attribute or nesting level exceeds the limits. A broken promise
///          aborts, so the fuzzer reports it as a finding. Built and run by script/feature/knx/run-fuzz.sh.
#ifndef PCH
    #include <kmx/aio/knx/secure/detail/keyring_format.hpp>
    #include <kmx/aio/knx/secure/detail/xml_reader.hpp>

    #include <cstddef>
    #include <cstdint>
    #include <cstdlib>
    #include <string_view>
#endif

namespace kmx::aio::fuzz::knx::xml_reader_fuzz
{
    namespace kd = kmx::aio::knx::secure::detail;

    /// @brief Aborts when an accepted element breaks a limit the reader promises to enforce.
    static void check_element(const kd::xml_event& event, const kd::xml_limits& limits) noexcept
    {
        if ((event.name.size() > limits.max_name_size) || (event.attributes.size() > limits.max_attributes))
            std::abort();
        for (const auto& attribute: event.attributes)
        {
            if ((attribute.value.size() > limits.max_value_size) || (kd::find_attribute(event, attribute.name) != &attribute.value))
                std::abort();
            static_cast<void>(kd::base64_decode(attribute.value));
        }
    }

    /// @brief Aborts when accepted events do not balance or nest deeper than the limit.
    static void check_events(const kd::xml_events_t& events) noexcept
    {
        const kd::xml_limits limits {};
        std::size_t depth {};
        for (const auto& event: events)
        {
            if ((event.kind == kd::xml_event_kind::end) && (depth == 0u))
                std::abort();
            depth = (event.kind == kd::xml_event_kind::start) ? (depth + 1u) : (depth - 1u);
            if (depth > limits.max_depth)
                std::abort();
            check_element(event, limits);
        }

        if ((depth != 0u) || (events.size() > (2u * limits.max_elements)))
            std::abort();
    }
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size)
{
    namespace target = kmx::aio::fuzz::knx::xml_reader_fuzz;
    const auto events = target::kd::read_xml(std::string_view {reinterpret_cast<const char*>(data), size});
    if (events.has_value())
        target::check_events(*events);
    return 0;
}
