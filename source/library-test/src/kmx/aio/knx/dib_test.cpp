/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/dib.hpp>
#include <kmx/aio/knx/discovery.hpp>
#include <kmx/aio/knx/routing.hpp>
#include <kmx/aio/knx/secure.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace kmx::aio::test::knx::dib_test
{
    using namespace kmx::aio::knx;

    // A DEVICE_INFO block is fixed at 54 octets, and the field order is the part no round-trip test can
    // check: encoder and decoder agree with each other whatever order they both use.
    TEST_CASE("knx dib device info matches its wire layout", "[knx][dib][unit]")
    {
        dib::device_info info {};
        info.knx_medium = dib::medium::ip;
        info.device_status = 0x01u; // programming mode
        info.address = individual_address {1u, 1u, 10u};
        info.project_installation_id = 0x1234u;
        info.serial_number = {0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u};
        info.multicast_address = {224u, 0u, 23u, 12u};
        info.mac_address = {0x0Au, 0x0Bu, 0x0Cu, 0x0Du, 0x0Eu, 0x0Fu};
        info.set_name("kmx interface");

        std::array<std::uint8_t, 54u> encoded {};
        const auto size = dib::encode(encoded, dib::block {info});
        REQUIRE(size.has_value());
        CHECK(*size == 54u);

        CHECK(encoded[0u] == 54u);    // structure length
        CHECK(encoded[1u] == 0x01u);  // device info
        CHECK(encoded[2u] == 0x20u);  // IP medium
        CHECK(encoded[3u] == 0x01u);  // programming mode
        CHECK(encoded[4u] == 0x11u);  // individual address 1.1.10
        CHECK(encoded[5u] == 0x0Au);
        CHECK(encoded[6u] == 0x12u);  // project installation id
        CHECK(encoded[7u] == 0x34u);
        CHECK(encoded[14u] == 224u);  // multicast address
        CHECK(encoded[18u] == 0x0Au); // mac address
        CHECK(encoded[24u] == 'k');   // friendly name
        CHECK(encoded[53u] == 0u);    // NUL padded to the end

        const auto decoded = dib::decode(encoded);
        REQUIRE(decoded.has_value());
        const auto* held = std::get_if<dib::device_info>(&decoded.value());
        REQUIRE(held != nullptr);
        CHECK(held->address == individual_address {1u, 1u, 10u});
        CHECK(held->project_installation_id == 0x1234u);
        CHECK(held->programming_mode());
        CHECK(held->name() == "kmx interface");
    }

    TEST_CASE("knx dib device info rejects a wrong length", "[knx][dib][unit]")
    {
        std::array<std::uint8_t, 53u> packet {};
        packet[0u] = 53u;
        packet[1u] = 0x01u;
        CHECK(dib::decode(packet).error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx dib service families round-trip", "[knx][dib][unit]")
    {
        const dib::supported_service_families families {
            false,
            {
                {dib::service_family::core, 2u},
                {dib::service_family::device_management, 2u},
                {dib::service_family::tunnelling, 2u},
                {dib::service_family::routing, 1u},
            },
        };

        const std::array<std::uint8_t, 10u> expected {
            0x0Au, 0x02u, 0x02u, 0x02u, 0x03u, 0x02u, 0x04u, 0x02u, 0x05u, 0x01u,
        };

        std::array<std::uint8_t, 10u> encoded {};
        REQUIRE(dib::encode(encoded, dib::block {families}).has_value());
        CHECK(encoded == expected);

        const auto decoded = dib::decode(expected);
        REQUIRE(decoded.has_value());
        const auto* held = std::get_if<dib::supported_service_families>(&decoded.value());
        REQUIRE(held != nullptr);
        CHECK(!held->secured);
        REQUIRE(held->families.size() == 4u);
        CHECK(held->contains(dib::service_family::tunnelling));
        CHECK(held->contains(dib::service_family::tunnelling, 2u));
        CHECK(!held->contains(dib::service_family::tunnelling, 3u));
        CHECK(!held->contains(dib::service_family::security));
    }

    TEST_CASE("knx dib keeps the secured service families variant apart", "[knx][dib][unit]")
    {
        const dib::supported_service_families secured {true, {{dib::service_family::tunnelling, 2u}}};
        std::array<std::uint8_t, 4u> encoded {};
        REQUIRE(dib::encode(encoded, dib::block {secured}).has_value());
        CHECK(encoded[1u] == 0x06u);

        const auto decoded = dib::decode(encoded);
        REQUIRE(decoded.has_value());
        const auto* held = std::get_if<dib::supported_service_families>(&decoded.value());
        REQUIRE(held != nullptr);
        CHECK(held->secured);
    }

    TEST_CASE("knx dib preserves an unknown service family", "[knx][dib][unit]")
    {
        const std::array<std::uint8_t, 6u> packet {0x06u, 0x02u, 0x04u, 0x02u, 0x7Fu, 0x01u};
        const auto decoded = dib::decode(packet);
        REQUIRE(decoded.has_value());
        const auto* held = std::get_if<dib::unknown_block>(&decoded.value());
        REQUIRE(held != nullptr);
        CHECK(held->type_code == static_cast<std::uint8_t>(dib::block_type::supported_service_families));

        std::array<std::uint8_t, packet.size()> encoded {};
        REQUIRE(dib::encode(encoded, *decoded).has_value());
        CHECK(encoded == packet);
    }

    TEST_CASE("knx dib service families reject an odd body", "[knx][dib][unit]")
    {
        const std::array<std::uint8_t, 5u> packet {0x05u, 0x02u, 0x04u, 0x02u, 0x05u};
        CHECK(dib::decode(packet).error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx dib ip configuration blocks round-trip", "[knx][dib][unit]")
    {
        const dib::ip_config configured {{192u, 0u, 2u, 20u}, {255u, 255u, 255u, 0u}, {192u, 0u, 2u, 1u}, 0x07u, 0x02u};
        std::array<std::uint8_t, 16u> encoded {};
        REQUIRE(dib::encode(encoded, dib::block {configured}).has_value());
        CHECK(encoded[0u] == 16u);
        CHECK(encoded[1u] == 0x03u);

        const auto decoded = dib::decode(encoded);
        REQUIRE(decoded.has_value());
        const auto* held = std::get_if<dib::ip_config>(&decoded.value());
        REQUIRE(held != nullptr);
        CHECK(held->subnet_mask == ipv4::storage_t {255u, 255u, 255u, 0u});
        CHECK(held->assignment_method == 0x02u);

        const dib::current_ip_config current {
            {192u, 0u, 2u, 20u}, {255u, 255u, 255u, 0u}, {192u, 0u, 2u, 1u}, {192u, 0u, 2u, 2u}, 0x04u};
        std::array<std::uint8_t, 20u> current_encoded {};
        REQUIRE(dib::encode(current_encoded, dib::block {current}).has_value());
        CHECK(current_encoded[0u] == 20u);
        CHECK(current_encoded[1u] == 0x04u);
        CHECK(current_encoded[19u] == 0u); // reserved

        const auto current_decoded = dib::decode(current_encoded);
        REQUIRE(current_decoded.has_value());
        CHECK(std::get_if<dib::current_ip_config>(&current_decoded.value())->dhcp_server == ipv4::storage_t {192u, 0u, 2u, 2u});
    }

    TEST_CASE("knx dib knx addresses round-trip", "[knx][dib][unit]")
    {
        const dib::knx_addresses addresses {
            individual_address {1u, 1u, 0u},
            {individual_address {1u, 1u, 10u}, individual_address {1u, 1u, 11u}},
        };
        std::array<std::uint8_t, 8u> encoded {};
        REQUIRE(dib::encode(encoded, dib::block {addresses}).has_value());
        CHECK(encoded[0u] == 8u);
        CHECK(encoded[1u] == 0x05u);

        const auto decoded = dib::decode(encoded);
        REQUIRE(decoded.has_value());
        const auto* held = std::get_if<dib::knx_addresses>(&decoded.value());
        REQUIRE(held != nullptr);
        CHECK(held->device == individual_address {1u, 1u, 0u});
        REQUIRE(held->additional.size() == 2u);
        CHECK(held->additional[1u] == individual_address {1u, 1u, 11u});
    }

    TEST_CASE("knx dib manufacturer data round-trips", "[knx][dib][unit]")
    {
        const dib::manufacturer_data data {0x00C5u, {0xDEu, 0xADu}};
        std::array<std::uint8_t, 6u> encoded {};
        REQUIRE(dib::encode(encoded, dib::block {data}).has_value());
        CHECK(encoded[1u] == 0xFEu);

        const auto decoded = dib::decode(encoded);
        REQUIRE(decoded.has_value());
        const auto* held = std::get_if<dib::manufacturer_data>(&decoded.value());
        REQUIRE(held != nullptr);
        CHECK(held->manufacturer_id == 0x00C5u);
        CHECK(held->data == byte_buffer_t {0xDEu, 0xADu});
    }

    // A description containing a block this build has no name for must survive a decode and re-encode
    // unchanged, or a gateway that forwards descriptions would quietly strip them.
    TEST_CASE("knx dib preserves an unmodelled block", "[knx][dib][unit]")
    {
        const std::array<std::uint8_t, 5u> packet {0x05u, 0x07u, 0x01u, 0x02u, 0x03u};
        const auto decoded = dib::decode(packet);
        REQUIRE(decoded.has_value());
        const auto* held = std::get_if<dib::unknown_block>(&decoded.value());
        REQUIRE(held != nullptr);
        CHECK(held->type_code == 0x07u);

        std::array<std::uint8_t, 5u> reencoded {};
        REQUIRE(dib::encode(reencoded, decoded.value()).has_value());
        CHECK(reencoded == packet);
    }

    TEST_CASE("knx dib decodes and re-encodes a whole description", "[knx][dib][integration]")
    {
        dib::device_info info {};
        info.address = individual_address {1u, 1u, 0u};
        info.set_name("kmx");

        const std::vector<dib::block> blocks {
            dib::block {info},
            dib::block {dib::supported_service_families {false, {{dib::service_family::core, 2u},
                                                                 {dib::service_family::tunnelling, 2u}}}},
            dib::block {dib::knx_addresses {individual_address {1u, 1u, 0u}, {}}},
        };

        std::vector<std::uint8_t> encoded(dib::encoded_size(blocks), 0u);
        const auto written = dib::encode_all(encoded, blocks);
        REQUIRE(written.has_value());
        CHECK(*written == encoded.size());
        CHECK(dib::valid_blocks(encoded));

        const auto decoded = dib::decode_all(encoded);
        REQUIRE(decoded.has_value());
        REQUIRE(decoded->size() == 3u);
        REQUIRE(dib::find_device_info(*decoded) != nullptr);
        CHECK(dib::find_device_info(*decoded)->name() == "kmx");
        const auto* families = dib::find_service_families(*decoded);
        REQUIRE(families != nullptr);
        CHECK(families->contains(dib::service_family::tunnelling, 2u));
        CHECK(dib::find_service_families(*decoded, true) == nullptr);

        std::vector<std::uint8_t> reencoded(dib::encoded_size(*decoded), 0u);
        REQUIRE(dib::encode_all(reencoded, *decoded).has_value());
        CHECK(reencoded == encoded);
    }

    TEST_CASE("knx dib rejects a run that does not account for itself", "[knx][dib][unit]")
    {
        // A block claiming more octets than remain, and one claiming fewer than its own prologue.
        CHECK(!dib::valid_blocks(std::array<std::uint8_t, 3u> {0x08u, 0x02u, 0x04u}));
        CHECK(!dib::valid_blocks(std::array<std::uint8_t, 3u> {0x01u, 0x02u, 0x04u}));
        CHECK(dib::decode_all(std::array<std::uint8_t, 3u> {0x08u, 0x02u, 0x04u}).error() ==
              make_error_code(error::malformed_frame));
    }

    // The service family of a KNXnet/IP service is the high octet of its service type, which is what lets a
    // server decide whether it serves a datagram from the same list it advertises.
    TEST_CASE("knx dib names the family a service belongs to", "[knx][dib][unit]")
    {
        CHECK(dib::family_of(discovery::search_request_service).value() == dib::service_family::core);
        CHECK(dib::family_of(discovery::search_request_extended_service).value() == dib::service_family::core);
        CHECK(dib::family_of(connection::connect_request_service).value() == dib::service_family::core);
        CHECK(dib::family_of(frame::device_configuration_request_service).value() == dib::service_family::device_management);
        CHECK(dib::family_of(frame::tunnelling_request_service).value() == dib::service_family::tunnelling);
        CHECK(dib::family_of(frame::tunnelling_feature_get_service).value() == dib::service_family::tunnelling);
        CHECK(dib::family_of(routing::indication_service).value() == dib::service_family::routing);
        CHECK(dib::family_of(secure::knx_secure_wrapper_service).value() == dib::service_family::security);
        CHECK(dib::family_of(0xFF00u).error() == error::unsupported_service);
    }
}
