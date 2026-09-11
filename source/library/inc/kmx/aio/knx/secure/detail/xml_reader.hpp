/// @file inc/kmx/aio/knx/secure/detail/xml_reader.hpp
/// @brief A bounded reader for the subset of XML an ETS keyring is written in.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// A keyring is key material, so the part of the library that reads it is kept small and strict rather than
/// general. This reader accepts what ETS writes - an optional byte order mark and XML declaration, comments,
/// nested and empty elements, quoted attributes with the five predefined entities and numeric character
/// references - and refuses everything else outright: a DOCTYPE, an entity declaration, a CDATA section, a
/// processing instruction other than the declaration, and text content. Refusing a DOCTYPE is what closes
/// the entity-expansion and external-entity classes, without depending on a general parser being configured
/// safely.
///
/// Attribute values come back decoded and normalised exactly as a conforming SAX parser reports them,
/// because the keyring signature is computed over those values and not over the raw text.
/// @reference W3C Extensible Markup Language (XML) 1.0, sections 2.3, 2.11 and 3.3.3.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <cstddef>
        #include <cstdint>
        #include <expected>
        #include <string>
        #include <string_view>
        #include <system_error>
        #include <vector>
    #endif

namespace kmx::aio::knx::secure::detail
{
    /// @brief The bounds a document is read within; exceeding any of them refuses the document.
    struct xml_limits
    {
        /// @brief Largest document, in octets.
        std::size_t max_document_size = 1u << 20u;
        /// @brief Most elements in the document.
        std::size_t max_elements = 4096u;
        /// @brief Most attributes on one element.
        std::size_t max_attributes = 16u;
        /// @brief Deepest element nesting, the root counted as one.
        std::size_t max_depth = 8u;
        /// @brief Longest element or attribute name, in octets.
        std::size_t max_name_size = 64u;
        /// @brief Longest decoded attribute value, in octets.
        std::size_t max_value_size = 4096u;
    };

    /// @brief One attribute, its value decoded and normalised.
    struct xml_attribute
    {
        /// @brief The attribute name.
        std::string name {};
        /// @brief The decoded value.
        std::string value {};
    };

    /// @brief Whether an event opens or closes an element.
    enum class xml_event_kind : std::uint8_t
    {
        /// @brief An element starts; an empty element produces a start and an end.
        start,
        /// @brief An element ends.
        end,
    };

    /// @brief One element boundary, in document order.
    struct xml_event
    {
        /// @brief Whether the element starts or ends here.
        xml_event_kind kind = xml_event_kind::start;
        /// @brief The element name.
        std::string name {};
        /// @brief The attributes, in document order; empty on an end event.
        std::vector<xml_attribute> attributes {};
    };

    /// @brief Every element boundary of a document, in order.
    using xml_events_t = std::vector<xml_event>;
    /// @brief The events of a document, or why it was refused.
    using xml_events_result_t = std::expected<xml_events_t, std::error_code>;

    /// @brief Reads a document into its element events.
    /// @param document The document octets.
    /// @param limits The bounds to read within.
    /// @return The events.
    /// @retval kmx::aio::knx::error::invalid_length The document is empty or larger than the limit.
    /// @retval kmx::aio::knx::error::malformed_frame The document is not well formed, uses a construct
    ///         outside the subset, or exceeds a structural limit.
    /// @throws std::bad_alloc when the events cannot be stored.
    [[nodiscard]] xml_events_result_t read_xml(std::string_view document, const xml_limits& limits = {}) noexcept(false);

    /// @brief Finds an attribute of an event by name.
    /// @param event The event to search.
    /// @param name The attribute name.
    /// @return The decoded value, or null when the element has no such attribute.
    [[nodiscard]] const std::string* find_attribute(const xml_event& event, std::string_view name) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
