/// @file src/kmx/aio/knx/secure/detail/xml_reader_test.cpp
/// @brief Unit tests for the keyring XML reader and base64 decoder: well-formedness, refused constructs and limits.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/detail/xml_reader.hpp>
#ifndef PCH
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/secure/detail/keyring_format.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <algorithm>
    #include <array>
    #include <string>
    #include <string_view>
    #include <utility>
#endif

namespace kmx::aio::test::knx::secure::detail::xml_reader_test
{
    namespace kd = kmx::aio::knx::secure::detail;
    using kmx::aio::knx::error;
    using kmx::aio::knx::make_error_code;

    namespace detail
    {
        /// @brief Indicates whether a document is refused as malformed.
        [[nodiscard]] bool refused(const std::string_view document) noexcept(false)
        {
            const auto events = kd::read_xml(document);
            return !events.has_value() && (events.error() == make_error_code(error::malformed_frame));
        }

        /// @brief Builds elements nested @p depth deep.
        [[nodiscard]] std::string nested(const std::size_t depth) noexcept(false)
        {
            std::string document {};
            for (std::size_t level {}; level < depth; ++level)
                document += "<a>";
            for (std::size_t level {}; level < depth; ++level)
                document += "</a>";
            return document;
        }

        /// @brief Builds one empty element carrying @p count attributes.
        [[nodiscard]] std::string with_attributes(const std::size_t count) noexcept(false)
        {
            std::string document = "<a";
            for (std::size_t index {}; index < count; ++index)
                document += " a" + std::to_string(index) + "=\"\"";
            return document + "/>";
        }

        /// @brief Views text as octets.
        [[nodiscard]] cspan_uint8_t octets(const std::string_view text) noexcept
        {
            return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
        }
    }

    TEST_CASE("knx xml reader reports elements and decoded attributes in document order", "[knx][keyring][unit]")
    {
        const std::string_view document = "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n<!-- exported -->\r\n"
                                          "<Root a=\"x &amp; &lt;&gt;&quot;&apos; &#65;&#x42;&#233;&#x1F600;\" b='line1&#10;line2'>\r\n"
                                          "  <Child c=\"t\tu\r\nv\"/>\r\n  <!-- inner -->\r\n</Root>\r\n<!-- trailing -->\r\n";
        const auto events = kd::read_xml(document);
        REQUIRE(events.has_value());
        REQUIRE(events->size() == 4u);

        const auto& root = events->at(0u);
        CHECK(root.kind == kd::xml_event_kind::start);
        CHECK(root.name == "Root");
        REQUIRE(root.attributes.size() == 2u);
        // Entities and references decode; a character reference is not normalised, so &#10; stays a line feed.
        CHECK(*kd::find_attribute(root, "a") == "x & <>\"' AB\xC3\xA9\xF0\x9F\x98\x80");
        CHECK(*kd::find_attribute(root, "b") == "line1\nline2");
        CHECK(kd::find_attribute(root, "missing") == nullptr);

        // A literal tab, and a CR LF pair, each read as one space.
        CHECK(events->at(1u).name == "Child");
        CHECK(*kd::find_attribute(events->at(1u), "c") == "t u v");
        CHECK(events->at(2u).kind == kd::xml_event_kind::end);
        CHECK(events->at(2u).name == "Child");
        CHECK(events->at(3u).kind == kd::xml_event_kind::end);
        CHECK(events->at(3u).name == "Root");
    }

    TEST_CASE("knx xml reader refuses constructs a keyring never contains", "[knx][keyring][unit]")
    {
        CHECK(detail::refused("<!DOCTYPE Keyring [<!ENTITY x \"y\">]><Keyring/>"));
        CHECK(detail::refused("<Keyring><!ENTITY x \"y\"></Keyring>"));
        CHECK(detail::refused("<Keyring><![CDATA[x]]></Keyring>"));
        CHECK(detail::refused("<Keyring><?php echo 1;?></Keyring>"));
        CHECK(detail::refused("<?xml-stylesheet href=\"x\"?><Keyring/>"));
        CHECK(detail::refused("<Keyring>text</Keyring>"));
        CHECK(detail::refused("<Keyring/><Second/>"));
    }

    TEST_CASE("knx xml reader refuses documents that are not well formed", "[knx][keyring][unit]")
    {
        CHECK(detail::refused("<Keyring>"));
        CHECK(detail::refused("<Keyring></Other>"));
        CHECK(detail::refused("<Keyring a=\"1\" a=\"2\"/>"));
        CHECK(detail::refused("<Keyring a=\"&nbsp;\"/>"));
        CHECK(detail::refused("<Keyring a=\"<\"/>"));
        CHECK(detail::refused("<Keyring a=\"1\"b=\"2\"/>"));
        CHECK(detail::refused("<Keyring a=1/>"));
        CHECK(detail::refused("<Keyring a=\"&#0;\"/>"));
        CHECK(detail::refused("<Keyring a=\"&#xD800;\"/>"));
        CHECK(detail::refused("<Keyring a=\"&#X41;\"/>"));
        CHECK(detail::refused("<Keyring a=\"unterminated/>"));
        CHECK(detail::refused("<!-- never closed <Keyring/>"));
        CHECK(detail::refused("   "));
    }

    TEST_CASE("knx xml reader enforces its limits", "[knx][keyring][unit]")
    {
        CHECK(!detail::refused(detail::nested(8u)));
        CHECK(detail::refused(detail::nested(9u)));
        CHECK(!detail::refused(detail::with_attributes(16u)));
        CHECK(detail::refused(detail::with_attributes(17u)));
        CHECK(kd::read_xml("").error() == make_error_code(error::invalid_length));
        CHECK(kd::read_xml(std::string((1u << 20u) + 1u, ' ')).error() == make_error_code(error::invalid_length));
        CHECK(detail::refused("<a v=\"" + std::string(4097u, 'x') + "\"/>"));
        CHECK(detail::refused("<" + std::string(65u, 'n') + "/>"));
    }

    TEST_CASE("knx keyring base64 matches RFC 4648 and refuses anything else", "[knx][keyring][unit]")
    {
        // RFC 4648 section 10.
        const std::array<std::pair<std::string_view, std::string_view>, 6u> vectors {{
            {"f", "Zg=="},
            {"fo", "Zm8="},
            {"foo", "Zm9v"},
            {"foob", "Zm9vYg=="},
            {"fooba", "Zm9vYmE="},
            {"foobar", "Zm9vYmFy"},
        }};
        for (const auto& [plain, encoded]: vectors)
        {
            INFO("plain=" << plain);
            CHECK(kd::base64_encode(detail::octets(plain)) == encoded);
            const auto decoded = kd::base64_decode(encoded);
            REQUIRE(decoded.has_value());
            CHECK(std::ranges::equal(*decoded, detail::octets(plain)));
        }

        for (const std::string_view bad: {"", "Zg=", "Z===", "Zm9v!A==", "Zg==Zg==", "=Zg=", "Zh==", "Zm9="})
        {
            INFO("bad=" << bad);
            CHECK(!kd::base64_decode(bad).has_value());
        }
    }
}
