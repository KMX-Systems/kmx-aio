/// @file src/kmx/aio/knx/keyring_test.cpp
/// @brief Unit tests for the ETS keyring reader: project exports, the credentials built from them, and refused documents.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/keyring.hpp>
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/ipv4.hpp>
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/group_address.hpp>
    #include <kmx/aio/knx/individual_address.hpp>
    #include <kmx/aio/knx/keyring/document.hpp>
    #include <kmx/aio/knx/secure/common.hpp>
    #include <kmx/aio/knx/secure/detail/crypto.hpp>
    #include <kmx/aio/knx/secure/detail/keyring_format.hpp>
    #include <kmx/aio/knx/secure/detail/xml_reader.hpp>
    #include <kmx/aio/knx/secure/key.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <algorithm>
    #include <array>
    #include <cstdint>
    #include <filesystem>
    #include <fstream>
    #include <iterator>
    #include <random>
    #include <string>
    #include <string_view>
    #include <vector>
#endif

namespace kmx::aio::test::knx::keyring_test
{
    namespace kr = kmx::aio::knx::keyring;
    namespace ks = kmx::aio::knx::secure;
    namespace kd = kmx::aio::knx::secure::detail;
    using kmx::aio::knx::error;
    using kmx::aio::knx::group_address;
    using kmx::aio::knx::individual_address;
    using kmx::aio::knx::make_error_code;

    namespace detail
    {
        /// @brief Finds the vendored fixtures from this source file, whatever the working directory is.
        [[nodiscard]] std::filesystem::path fixture_directory() noexcept(false)
        {
            auto directory = std::filesystem::absolute(std::filesystem::path {__FILE__}).parent_path();
            while ((directory != directory.parent_path()) && !std::filesystem::exists(directory / "documentation"))
                directory = directory.parent_path();
            return directory / "documentation" / "features" / "knx" / "conformance" / "keyrings";
        }

        /// @brief Reads one fixture's octets; empty when it is missing, which every test then fails on.
        [[nodiscard]] std::string read_fixture(const std::string_view name) noexcept(false)
        {
            std::ifstream input(fixture_directory() / name, std::ios::binary);
            return {std::istreambuf_iterator<char> {input}, std::istreambuf_iterator<char> {}};
        }

        /// @brief Returns the value of one hexadecimal digit, or -1 for anything else.
        [[nodiscard]] int nibble(const char value) noexcept
        {
            if ((value >= '0') && (value <= '9'))
                return value - '0';
            if ((value >= 'a') && (value <= 'f'))
                return value - 'a' + 10;
            return -1;
        }

        /// @brief Compares octets against lower-case hexadecimal text.
        [[nodiscard]] bool equal(const cspan_uint8_t actual, const std::string_view expected) noexcept
        {
            if (expected.size() != (2u * actual.size()))
                return false;
            for (std::size_t index {}; index < actual.size(); ++index)
                if (actual[index] != static_cast<std::uint8_t>((nibble(expected[2u * index]) << 4) | nibble(expected[(2u * index) + 1u])))
                    return false;
            return true;
        }

        [[nodiscard]] individual_address individual(const std::string_view text) noexcept
        {
            return individual_address::parse(text).value_or(individual_address {});
        }

        /// @brief Indicates whether a result holds exactly the expected error.
        template <typename Result>
        [[nodiscard]] bool refused_with(const Result& result, const error expected) noexcept
        {
            return !result.has_value() && (result.error() == make_error_code(expected));
        }

        /// @brief Describes a load, so a keyring that fails to load reports why and not only that it failed.
        [[nodiscard]] std::string outcome(const kr::document_result_t& result) noexcept(false)
        {
            return result.has_value() ? std::string {"loaded"} : result.error().message();
        }

        /// @brief Signs a keyring the way ETS does, so its contents can be malformed beneath a valid signature.
        /// @param document A keyring whose root carries `Signature=""`.
        /// @param password_hash The keyring password hash.
        [[nodiscard]] std::string sign(std::string document, const ks::secret_key& password_hash) noexcept(false)
        {
            const auto events = kd::read_xml(document);
            REQUIRE(events.has_value());
            const auto stream = kd::keyring_signature_stream(*events, password_hash);
            REQUIRE(stream.has_value());
            std::array<std::uint8_t, kd::sha256_size> digest {};
            REQUIRE(kd::evp_backend().sha256(*stream, digest));

            const std::string placeholder = "Signature=\"\"";
            const auto signature = kd::base64_encode(cspan_uint8_t {digest.data(), ks::key_size});
            document.replace(document.find(placeholder), placeholder.size(), "Signature=\"" + signature + "\"");
            return document;
        }

        /// @brief A keyring created at 2026-01-01T00:00:00, unsigned, with @p body inside its root.
        [[nodiscard]] std::string synthetic(const std::string_view body) noexcept(false)
        {
            return "<Keyring Project=\"synthetic\" CreatedBy=\"kmx-aio test\" Created=\"2026-01-01T00:00:00\" Signature=\"\" "
                   "xmlns=\"http://knx.org/xml/keyring/1\">" +
                   std::string {body} + "</Keyring>";
        }
    }

    TEST_CASE("knx keyring reads the backbone and tunnels of a full project export", "[knx][keyring][unit]")
    {
        const auto keyring = kr::load(detail::read_fixture("keyring.knxkeys"), "pwd");
        REQUIRE(detail::outcome(keyring) == "loaded");
        CHECK(keyring->project == "KeyringTest");
        CHECK(keyring->created_by == "ETS 5.7.2 (Build 743)");
        CHECK(keyring->created == "2019-06-11T06:45:22");
        REQUIRE(keyring->backbone_entry.has_value());
        CHECK(keyring->backbone_entry->latency_ms == 1000u);
        CHECK(keyring->backbone_entry->multicast_address == ipv4::storage_t {224u, 0u, 23u, 12u});
        CHECK(detail::equal(keyring->backbone_entry->key.bytes(), "96f034fccf510760cbd63da0f70d4a9d"));

        REQUIRE(keyring->interfaces.size() == 10u);
        const auto* const tunnel = keyring->find_interface(detail::individual("1.1.4"));
        REQUIRE(tunnel != nullptr);
        CHECK(tunnel->type == kr::interface_type::tunnelling);
        CHECK(tunnel->host == detail::individual("1.1.0"));
        CHECK(tunnel->user_id == std::uint8_t {2u});
        CHECK(tunnel->user_password.view() == "user4");
        CHECK(tunnel->device_authentication.view() == "dev");
        CHECK(keyring->find_interface(detail::individual("1.1.6"))->user_password.view() == "@zvI1G&_");
        CHECK(keyring->find_interface(detail::individual("1.1.7"))->user_password.view() == "ZvDY-:g#");
        CHECK(keyring->find_interface(detail::individual("1.1.8"))->user_password.view() == "Kr;)20d%");

        const auto* const receiver = keyring->find_interface(detail::individual("1.1.20"));
        REQUIRE(receiver != nullptr);
        CHECK(receiver->host == detail::individual("1.1.10"));
        CHECK(!receiver->user_id.has_value());
        CHECK(receiver->user_password.empty());
        REQUIRE(receiver->groups.size() == 1u);
        CHECK(receiver->groups.front().address == group_address {2305u});
        CHECK(receiver->groups.front().senders ==
              std::vector<individual_address> {detail::individual("1.1.1"), detail::individual("1.1.12")});
    }

    TEST_CASE("knx keyring reads the group keys and devices of a full project export", "[knx][keyring][unit]")
    {
        const auto keyring = kr::load(detail::read_fixture("keyring.knxkeys"), "pwd");
        REQUIRE(detail::outcome(keyring) == "loaded");
        REQUIRE(keyring->group_keys.size() == 1u);
        const auto* const group = keyring->find_group_key(group_address {2305u});
        REQUIRE(group != nullptr);
        CHECK(detail::equal(group->key.bytes(), "e14343050f4377e3159b90afe0228216"));

        REQUIRE(keyring->devices.size() == 3u);
        const auto* const router = keyring->find_device(detail::individual("1.1.0"));
        REQUIRE(router != nullptr);
        CHECK(router->sequence_number == 108u);
        CHECK(detail::equal(router->tool_key.bytes(), "aeac47c4653ed0b25249b4ab3f474479"));
        CHECK(router->management_password.view() == "router1");
        CHECK(router->authentication.view() == "dev");
        const auto* const other = keyring->find_device(detail::individual("1.1.10"));
        REQUIRE(other != nullptr);
        CHECK(other->authentication.view() == "flXo@ 'O");
        CHECK(other->management_password.view() == "fy.V&bcf");
        CHECK(other->sequence_number == 0u);
        CHECK(keyring->find_device(detail::individual("1.1.99")) == nullptr);
    }

    TEST_CASE("knx keyring reads an export with a byte order mark and a 48-bit sequence number", "[knx][keyring][unit]")
    {
        const auto keyring = kr::load(detail::read_fixture("testcase.knxkeys"), "password");
        REQUIRE(detail::outcome(keyring) == "loaded");
        CHECK(keyring->project == "Why do you care?");
        REQUIRE(keyring->backbone_entry.has_value());
        CHECK(detail::equal(keyring->backbone_entry->key.bytes(), "cf89fd0f18f4889783c7ef44ee1f5e14"));
        REQUIRE(keyring->interfaces.size() == 4u);
        CHECK(keyring->find_interface(detail::individual("1.0.1"))->user_password.view() == "user1");
        CHECK(keyring->find_interface(detail::individual("1.0.1"))->user_id == std::uint8_t {3u});
        CHECK(keyring->find_interface(detail::individual("1.0.13"))->user_password.view() == "user4");
        CHECK(keyring->find_interface(detail::individual("1.0.13"))->device_authentication.view() == "authenticationcode");

        REQUIRE(keyring->devices.size() == 1u);
        CHECK(keyring->devices.front().sequence_number == 133294561196u);
        CHECK(keyring->devices.front().management_password.view() == "commissioning");
        CHECK(keyring->devices.front().authentication.view() == "authenticationcode");
        CHECK(detail::equal(keyring->devices.front().tool_key.bytes(), "9bc4fc74043a332b80baa2c8fef72d9d"));
    }

    TEST_CASE("knx keyring signs UTF-8 attribute values as octets", "[knx][keyring][unit]")
    {
        const auto keyring = kr::load(detail::read_fixture("special_chars_secure_tunnel.knxkeys"), "test");
        REQUIRE(detail::outcome(keyring) == "loaded");
        CHECK(keyring->project.starts_with("Project name with special chars \xC3\xA4"));
        CHECK(!keyring->backbone_entry.has_value());
        REQUIRE(keyring->interfaces.size() == 5u);
        for (std::uint8_t device = 2u; device <= 6u; ++device)
        {
            const auto* const tunnel = keyring->find_interface(individual_address {1u, 0u, device});
            REQUIRE(tunnel != nullptr);
            CHECK(tunnel->user_password.view() == ("tunnel_" + std::to_string(device)));
        }

        REQUIRE(keyring->devices.size() == 1u);
        CHECK(detail::equal(keyring->devices.front().tool_key.bytes(), "90870edb344bb79b072081270664b508"));
    }

    TEST_CASE("knx keyring reads Data Secure group keys and senders", "[knx][keyring][unit]")
    {
        const auto keyring = kr::load(detail::read_fixture("DataSecure_only_one_interface.knxkeys"), "test");
        REQUIRE(detail::outcome(keyring) == "loaded");
        CHECK(keyring->created_by == "ETS 5.7.7 (Build 1428)");
        CHECK(keyring->created == "2023-02-06T21:17:09");
        REQUIRE(keyring->interfaces.size() == 1u);
        const auto& receiver = keyring->interfaces.front();
        CHECK(receiver.address == detail::individual("1.0.4"));
        CHECK(!receiver.user_id.has_value());
        CHECK(receiver.user_password.empty());
        REQUIRE(receiver.groups.size() == 3u);
        CHECK(receiver.groups[0u].address == group_address {65535u});
        CHECK(receiver.groups[0u].senders.empty());
        CHECK(receiver.groups[1u].senders == std::vector<individual_address> {detail::individual("1.0.1"), detail::individual("1.0.2")});
        REQUIRE(keyring->group_keys.size() == 3u);
        CHECK(detail::equal(keyring->find_group_key(group_address {1u})->key.bytes(), "b9434066177467de2ee9f8c5ea8496d8"));
        CHECK(detail::equal(keyring->find_group_key(group_address {3u})->key.bytes(), "e926a466ef451eb7e1487bf8dd5cd32f"));
        CHECK(detail::equal(keyring->find_group_key(group_address {65535u})->key.bytes(), "d2effbdc88eb5c9a57a73b0aad64996f"));

        const auto usb = kr::load(detail::read_fixture("DataSecure_usb.knxkeys"), "test");
        REQUIRE(detail::outcome(usb) == "loaded");
        REQUIRE(usb->interfaces.size() == 1u);
        CHECK(usb->interfaces.front().type == kr::interface_type::usb);
        CHECK(!usb->interfaces.front().host.has_value());
        CHECK(usb->interfaces.front().groups.front().senders == std::vector<individual_address> {detail::individual("1.0.4")});
        CHECK(detail::equal(usb->group_keys.front().key.bytes(), "d2effbdc88eb5c9a57a73b0aad64996f"));
    }

    TEST_CASE("knx keyring builds tunnelling credentials and routing configuration", "[knx][keyring][unit]")
    {
        static constexpr ks::serial_number_t serial {0x00u, 0xFAu, 0x12u, 0x34u, 0x56u, 0x78u};
        const auto keyring = kr::load(detail::read_fixture("keyring.knxkeys"), "pwd");
        REQUIRE(detail::outcome(keyring) == "loaded");

        // The expected keys are PBKDF2-HMAC-SHA256 of "user4" and "dev" under their KNX salts, computed with hashlib.
        const auto credentials = kr::credentials_for(*keyring, detail::individual("1.1.4"), serial);
        REQUIRE(credentials.has_value());
        CHECK(credentials->user_id == 2u);
        CHECK(detail::equal(credentials->user_password_key.bytes(), "860f6dcf3bdf4223231172c5b23d30ff"));
        CHECK(detail::equal(credentials->device_authentication_code.bytes(), "2af9c6171b21d97a84905282f95f8e60"));
        CHECK(!credentials->skip_device_authentication);
        CHECK(credentials->serial_number == serial);

        const auto routing = kr::routing_configuration_for(*keyring, serial);
        REQUIRE(routing.has_value());
        CHECK(detail::equal(routing->backbone_key.bytes(), "96f034fccf510760cbd63da0f70d4a9d"));
        CHECK(routing->multicast_address == ipv4::storage_t {224u, 0u, 23u, 12u});
        CHECK(routing->latency_tolerance_ms == 1000u);
        CHECK(routing->serial_number == serial);
        // The configuration holds a copy; the document keeps its own key.
        CHECK(detail::equal(keyring->backbone_entry->key.bytes(), "96f034fccf510760cbd63da0f70d4a9d"));
    }

    TEST_CASE("knx keyring builds a secure tunnelling server's configuration from the tunnels on one host", "[knx][keyring][unit]")
    {
        static constexpr ks::serial_number_t serial {0x00u, 0xFAu, 0x12u, 0x34u, 0x56u, 0x78u};
        const auto keyring = kr::load(detail::read_fixture("testcase.knxkeys"), "password");
        REQUIRE(detail::outcome(keyring) == "loaded");

        const auto configuration = kr::server_configuration_for(*keyring, detail::individual("1.0.0"), serial);
        REQUIRE(configuration.has_value());
        CHECK(configuration->serial_number == serial);
        const auto device_code = ks::derive_device_authentication_code("authenticationcode");
        REQUIRE(device_code.has_value());
        CHECK(std::ranges::equal(configuration->device_authentication_code.bytes(), device_code->bytes()));
        // The four tunnelling slots on 1.0.0 are four users, in document order, each with its own tunnel address.
        REQUIRE(configuration->users.size() == 4u);
        CHECK(configuration->users.front().user_id == 3u);
        CHECK(configuration->users.front().tunnel_addresses == std::vector<individual_address> {detail::individual("1.0.1")});
        const auto user1 = ks::derive_user_password_key("user1");
        REQUIRE(user1.has_value());
        CHECK(std::ranges::equal(configuration->users.front().password_key.bytes(), user1->bytes()));
        CHECK(configuration->users.back().user_id == 6u);
        CHECK(configuration->users.back().tunnel_addresses == std::vector<individual_address> {detail::individual("1.0.13")});

        CHECK(detail::refused_with(kr::server_configuration_for(*keyring, detail::individual("1.0.0"), ks::serial_number_t {}),
                                   error::invalid_configuration));
        CHECK(
            detail::refused_with(kr::server_configuration_for(*keyring, detail::individual("1.0.99"), serial), error::secure_key_missing));
    }

    TEST_CASE("knx keyring refuses credentials it cannot build", "[knx][keyring][unit]")
    {
        static constexpr ks::serial_number_t serial {0x00u, 0xFAu, 0x12u, 0x34u, 0x56u, 0x78u};
        const auto keyring = kr::load(detail::read_fixture("keyring.knxkeys"), "pwd");
        REQUIRE(detail::outcome(keyring) == "loaded");

        // P8: there is no default serial number, so the all-zero one an unset field holds is refused.
        CHECK(detail::refused_with(kr::credentials_for(*keyring, detail::individual("1.1.4"), ks::serial_number_t {}),
                                   error::invalid_configuration));
        CHECK(detail::refused_with(kr::routing_configuration_for(*keyring, ks::serial_number_t {}), error::invalid_configuration));
        CHECK(detail::refused_with(kr::credentials_for(*keyring, detail::individual("1.1.99"), serial), error::secure_key_missing));
        // 1.1.20 only receives group traffic: it has no user id and no password.
        CHECK(detail::refused_with(kr::credentials_for(*keyring, detail::individual("1.1.20"), serial), error::secure_key_missing));

        const auto without_backbone = kr::load(detail::read_fixture("special_chars_secure_tunnel.knxkeys"), "test");
        REQUIRE(detail::outcome(without_backbone) == "loaded");
        CHECK(detail::refused_with(kr::routing_configuration_for(*without_backbone, serial), error::secure_key_missing));
    }

    TEST_CASE("knx keyring refuses a wrong password and an altered document", "[knx][keyring][unit]")
    {
        const auto original = detail::read_fixture("keyring.knxkeys");
        REQUIRE(!original.empty());
        CHECK(detail::refused_with(kr::load(original, "wrong"), error::keyring_signature_invalid));
        CHECK(detail::refused_with(kr::load(detail::read_fixture("testcase.knxkeys"), "wrong_password"), error::keyring_signature_invalid));

        auto latency = original;
        latency.replace(latency.find("Latency=\"1000\""), 14u, "Latency=\"1001\"");
        CHECK(detail::refused_with(kr::load(latency, "pwd"), error::keyring_signature_invalid));

        auto senders = original;
        senders.replace(senders.find("Senders=\"1.1.12\""), 16u, "Senders=\"1.1.13\"");
        CHECK(detail::refused_with(kr::load(senders, "pwd"), error::keyring_signature_invalid));

        // xmlns is not signed, so removing it changes nothing a keyring's signature covers.
        auto namespace_free = original;
        const std::string_view xmlns = " xmlns=\"http://knx.org/xml/keyring/1\"";
        namespace_free.erase(namespace_free.find(xmlns), xmlns.size());
        CHECK(detail::outcome(kr::load(namespace_free, "pwd")) == "loaded");
    }

    TEST_CASE("knx keyring refuses malformed values beneath a valid signature", "[knx][keyring][unit]")
    {
        const auto hash = ks::derive_keyring_password_hash("test");
        REQUIRE(hash.has_value());
        CHECK(detail::equal(hash->bytes(), "6ec203b24e9ef4840eeef4b2229eee9b"));

        // First a well formed synthetic keyring, to show that the signing path itself is sound.
        const auto good =
            kr::load(detail::sign(detail::synthetic(
                                      "<Backbone Key=\"R3j2DTE2c1aCgctvdj8qHw==\"/>"
                                      "<Interface Type=\"Tunneling\" IndividualAddress=\"1.0.1\" Password=\"nKix+1kXwpdB0rF4bJkl+g==\"/>"),
                                  *hash),
                     *hash);
        REQUIRE(detail::outcome(good) == "loaded");
        CHECK(detail::equal(good->backbone_entry->key.bytes(), "000102030405060708090a0b0c0d0e0f"));
        CHECK(good->interfaces.front().user_password.view() == "pw");

        // Each optional value, well formed, is read rather than refused.
        const std::array<std::string_view, 4u> accepted {
            "<Backbone Key=\"R3j2DTE2c1aCgctvdj8qHw==\" MulticastAddress=\"224.0.23.12\"/>",
            "<Backbone Key=\"R3j2DTE2c1aCgctvdj8qHw==\" Latency=\"1000\"/>",
            "<Devices><Device IndividualAddress=\"1.0.1\" SequenceNumber=\"108\"/></Devices>",
            "<Interface Type=\"Tunneling\" IndividualAddress=\"1.0.1\" Host=\"1.0.0\" UserID=\"127\"/>",
        };
        for (const auto body: accepted)
        {
            INFO("body=" << body);
            CHECK(detail::outcome(kr::load(detail::sign(detail::synthetic(body), *hash), *hash)) == "loaded");
        }

        const std::array<std::string_view, 15u> bodies {
            "<Backbone Key=\"!!!!\"/>",
            "<Backbone Key=\"dzyMH+FGKmk//w70PbBoKX8M5IOpaF64UjhaTeAUqZE=\"/>",
            "<Backbone/>",
            "<Backbone Key=\"R3j2DTE2c1aCgctvdj8qHw==\" Latency=\"70000\"/>",
            "<Backbone Key=\"R3j2DTE2c1aCgctvdj8qHw==\" MulticastAddress=\"224.0.23\"/>",
            "<Interface Type=\"Tunneling\" IndividualAddress=\"1.0.1\" Password=\"DNRqH7NbCFlLz3iR92rq4A==\"/>",
            "<Interface Type=\"Tunneling\" IndividualAddress=\"1.0.1\" Password=\"cD2YEvqXbvxFV5g3mBzYzA==\"/>",
            "<Interface Type=\"Tunneling\" IndividualAddress=\"1.0.1\" Password=\"CXzHvBQ4IM5tdagS/1/pOg==\"/>",
            "<Interface Type=\"Serial\" IndividualAddress=\"1.0.1\"/>",
            "<Interface Type=\"Tunneling\" IndividualAddress=\"1.0\"/>",
            "<Interface Type=\"Tunneling\" IndividualAddress=\"1.0.1\" UserID=\"255\"/>",
            "<Interface Type=\"Tunneling\" IndividualAddress=\"1.0.1\" UserID=\"256\"/>",
            "<Interface Type=\"Tunneling\" IndividualAddress=\"1.0.1\"><Group Address=\"1\" Senders=\"1.0\"/></Interface>",
            "<GroupAddresses><Group Address=\"1\"/></GroupAddresses>",
            "<Devices><Device IndividualAddress=\"1.0.1\" SequenceNumber=\"281474976710656\"/></Devices>",
        };
        for (const auto body: bodies)
        {
            INFO("body=" << body);
            CHECK(detail::refused_with(kr::load(detail::sign(detail::synthetic(body), *hash), *hash), error::malformed_frame));
        }
    }

    TEST_CASE("knx keyring refuses documents that are not signable keyrings", "[knx][keyring][unit]")
    {
        const auto hash = ks::derive_keyring_password_hash("test");
        REQUIRE(hash.has_value());
        // The one-octet length prefix of the signature stream cannot describe a 256-octet value.
        const auto long_project =
            "<Keyring Project=\"" + std::string(256u, 'x') + "\" Created=\"x\" Signature=\"AAAAAAAAAAAAAAAAAAAAAA==\"/>";
        CHECK(detail::refused_with(kr::load(long_project, *hash), error::malformed_frame));
        CHECK(
            detail::refused_with(kr::load("<Other Created=\"x\" Signature=\"AAAAAAAAAAAAAAAAAAAAAA==\"/>", *hash), error::malformed_frame));
        CHECK(detail::refused_with(kr::load("<Keyring Signature=\"AAAAAAAAAAAAAAAAAAAAAA==\"/>", *hash), error::malformed_frame));
        CHECK(detail::refused_with(kr::load("<Keyring Created=\"x\"/>", *hash), error::malformed_frame));
        CHECK(detail::refused_with(kr::load("", *hash), error::invalid_length));
        CHECK(detail::refused_with(kr::load(std::string(kr::max_document_size + 1u, ' '), "test"), error::invalid_length));
    }

    TEST_CASE("knx keyring survives mutated exports", "[knx][keyring][unit]")
    {
        const auto hash = ks::derive_keyring_password_hash("pwd");
        REQUIRE(hash.has_value());
        const auto original = detail::read_fixture("keyring.knxkeys");
        REQUIRE(!original.empty());

        // A deterministic mutation smoke run: the point is not the verdicts, which are nearly all a broken
        // signature or a malformed document, but that no mutation crashes or overruns the reader.
        std::minstd_rand generator {20260910u};
        std::size_t loaded {};
        for (std::size_t iteration {}; iteration < 400u; ++iteration)
        {
            auto mutated = original;
            const auto flips = 1u + (generator() % 4u);
            for (std::size_t flip {}; flip < flips; ++flip)
                mutated[generator() % mutated.size()] = static_cast<char>(generator() & 0xFFu);
            if ((generator() % 8u) == 0u)
                mutated.resize(generator() % mutated.size());
            loaded += kr::load(mutated, *hash).has_value() ? 1u : 0u;
        }

        CHECK(loaded < 400u);
    }
}
