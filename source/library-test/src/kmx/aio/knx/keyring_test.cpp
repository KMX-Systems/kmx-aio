/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/keyring.hpp>
#include <kmx/aio/knx/secure.hpp>

#include <array>
#include <span>
#include <type_traits>

namespace internal
{
    class fixture_decryptor final: public kmx::aio::knx::keyring::decryptor
    {
    public:
        [[nodiscard]] std::expected<kmx::aio::knx::keyring::key_t, kmx::aio::knx::error>
        decrypt_key(const std::span<const std::uint8_t> encrypted_key, const std::string_view password_id) noexcept override
        {
            if (password_id != "ops")
                return std::unexpected(kmx::aio::knx::error::secure_unsupported);
            if (encrypted_key.size() != kmx::aio::knx::keyring::key_size)
                return std::unexpected(kmx::aio::knx::error::malformed_frame);

            kmx::aio::knx::keyring::key_t decrypted {};
            for (std::size_t index = 0u; index < encrypted_key.size(); ++index)
                decrypted[index] = encrypted_key[index] ^ 0xAAu;
            return decrypted;
        }
    };
} // namespace internal

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
        internal::fixture_decryptor decrypter {};

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

    // A keyring exists to supply the keys the secure boundary consumes, so the two must name one type. They
    // used to name two identical ones, which meant a key crossed the seam only because both happened to be
    // the same array - a coincidence a later edit to either could have broken silently.
    static_assert(std::is_same_v<kmx::aio::knx::keyring::key_t, kmx::aio::knx::secure::key_t>,
                  "a keyring key must be the very type a secure configuration holds");
    static_assert(kmx::aio::knx::keyring::key_size == kmx::aio::knx::secure::key_size);

    TEST_CASE("knx keyring key configures the secure boundary directly", "[knx][keyring][integration]")
    {
        const auto record = kmx::aio::knx::keyring::parse_selected(
            R"(<Key device-id="line-1" key-id="ops" key="00112233445566778899aabbccddeeff" />)", "line-1", "ops");
        REQUIRE(record.has_value());

        // Assigned across the seam without a copy through a third type or a reinterpretation.
        kmx::aio::knx::secure::configuration configuration {};
        configuration.selected = kmx::aio::knx::secure::profile::data_secure;
        configuration.key = record->key;

        CHECK(kmx::aio::knx::secure::validate(configuration).has_value());
        CHECK(configuration.key[0u] == 0x00u);
        CHECK(configuration.key[15u] == 0xFFu);
    }
}
