/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/keyring.hpp>

#include <array>
#include <span>

namespace
{
    class fixture_decryptor final: public kmx::aio::knx::keyring::decryptor
    {
    public:
        [[nodiscard]] std::expected<std::array<std::uint8_t, kmx::aio::knx::keyring::key_size>, kmx::aio::knx::error>
        decrypt_key(const std::span<const std::uint8_t> encrypted_key, const std::string_view password_id) noexcept override
        {
            if (password_id != "ops")
                return std::unexpected(kmx::aio::knx::error::secure_unsupported);
            if (encrypted_key.size() != kmx::aio::knx::keyring::key_size)
                return std::unexpected(kmx::aio::knx::error::malformed_frame);

            std::array<std::uint8_t, kmx::aio::knx::keyring::key_size> decrypted {};
            for (std::size_t index = 0u; index < encrypted_key.size(); ++index)
                decrypted[index] = encrypted_key[index] ^ 0xAAu;
            return decrypted;
        }
    };
}

namespace kmx::aio::test::knx::keyring_test
{
    TEST_CASE("knx keyring parses bounded hexadecimal key record", "[knx][keyring][unit]")
    {
        const auto record = kmx::aio::knx::keyring::parse(
            R"(<Key device-id="interface-1" key="00112233445566778899aabbccddeeff" />)");
        REQUIRE(record.has_value());
        CHECK(record->device_id == "interface-1");
        CHECK(record->key[0u] == 0x00u);
        CHECK(record->key[15u] == 0xFFu);
    }

    TEST_CASE("knx keyring rejects unsafe or malformed documents", "[knx][keyring][unit]")
    {
        CHECK(!kmx::aio::knx::keyring::parse("<!DOCTYPE Key><Key key=\"00\"/>").has_value());
        CHECK(!kmx::aio::knx::keyring::parse("<Key key=\"not-a-key\"/>").has_value());
        CHECK(!kmx::aio::knx::keyring::parse("<Key key=\"00112233445566778899aabbccddeeff\"").has_value());
    }

    TEST_CASE("knx keyring selects matching records by device and key id", "[knx][keyring][unit]")
    {
        const auto record = kmx::aio::knx::keyring::parse_selected(
            R"(
            <Keyring>
              <Key device-id="interface-a" key-id="ops" key="00000000000000000000000000000001" />
              <Key device-id="interface-b" key-id="ops" key="00112233445566778899aabbccddeeff" />
            </Keyring>
            )",
            "interface-b",
            "ops");
        REQUIRE(record.has_value());
        CHECK(record->device_id == "interface-b");
        CHECK(record->key[0u] == 0x00u);
        CHECK(record->key[15u] == 0xFFu);
    }

    TEST_CASE("knx keyring decrypts encrypted records through injected decryptor", "[knx][keyring][unit]")
    {
        fixture_decryptor decrypter {};

        const auto record = kmx::aio::knx::keyring::parse_selected(
            R"(<Key device-id="interface-secure" key-id="ops" password-id="ops" encrypted-key="aab988ffeeddcc332211005766554433" />)",
            "interface-secure",
            "ops",
            &decrypter);
        REQUIRE(record.has_value());
        CHECK(record->device_id == "interface-secure");
        CHECK(record->key[0u] == 0x00u);
        CHECK(record->key[1u] == 0x13u);
        CHECK(record->key[15u] == 0x99u);
    }

    TEST_CASE("knx keyring reports encrypted records as unsupported without decryptor", "[knx][keyring][unit]")
    {
        const auto record = kmx::aio::knx::keyring::parse_selected(
            R"(<Key device-id="interface-secure" key-id="ops" password-id="ops" encrypted-key="00112233445566778899aabbccddeeff" />)",
            "interface-secure",
            "ops");
        REQUIRE(!record.has_value());
        CHECK(record.error() == kmx::aio::knx::error::secure_unsupported);
    }
}
