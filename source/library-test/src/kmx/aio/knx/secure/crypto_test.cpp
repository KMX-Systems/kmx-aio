/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/error.hpp>
#include <kmx/aio/knx/secure/common.hpp>
#include <kmx/aio/knx/secure/detail/ccm.hpp>
#include <kmx/aio/knx/secure/detail/crypto.hpp>
#include <kmx/aio/knx/secure/entropy.hpp>
#include <kmx/aio/knx/secure/key.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace kmx::aio::test::knx::secure::crypto_test
{
    namespace ks = kmx::aio::knx::secure;
    namespace kd = kmx::aio::knx::secure::detail;
    using kmx::aio::knx::error;
    using kmx::aio::knx::make_error_code;

    namespace detail
    {
        /// @brief Returns the value of one hexadecimal digit, or -1 for anything else.
        [[nodiscard]] int nibble(const char value) noexcept
        {
            if ((value >= '0') && (value <= '9'))
                return value - '0';
            if ((value >= 'a') && (value <= 'f'))
                return value - 'a' + 10;
            if ((value >= 'A') && (value <= 'F'))
                return value - 'A' + 10;
            return -1;
        }

        /// @brief Decodes hexadecimal text into octets, ignoring anything that is not a digit.
        [[nodiscard]] std::vector<std::uint8_t> hex(const std::string_view text) noexcept(false)
        {
            std::vector<std::uint8_t> result {};
            int high = -1;
            for (const auto character: text)
            {
                const auto value = nibble(character);
                if (value < 0)
                    continue;
                if (high < 0)
                    high = value;
                else
                {
                    result.push_back(static_cast<std::uint8_t>((high << 4) | value));
                    high = -1;
                }
            }
            return result;
        }

        /// @brief Decodes hexadecimal text into a fixed-size array.
        template <std::size_t Size>
        [[nodiscard]] std::array<std::uint8_t, Size> fixed(const std::string_view text) noexcept(false)
        {
            const auto octets = hex(text);
            std::array<std::uint8_t, Size> result {};
            std::copy_n(octets.begin(), std::min(Size, octets.size()), result.begin());
            return result;
        }

        /// @brief Compares octets against hexadecimal text.
        [[nodiscard]] bool equal(const cspan_uint8_t actual, const std::string_view expected) noexcept(false)
        {
            const auto octets = hex(expected);
            return std::equal(actual.begin(), actual.end(), octets.begin(), octets.end());
        }

        /// @brief Views text as octets.
        [[nodiscard]] cspan_uint8_t octets(const std::string_view text) noexcept
        {
            return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
        }

        /// @brief Concatenates octet runs.
        [[nodiscard]] std::vector<std::uint8_t> join(std::vector<std::uint8_t> head, const cspan_uint8_t tail) noexcept(false)
        {
            head.insert(head.end(), tail.begin(), tail.end());
            return head;
        }

        [[nodiscard]] bool fail_cbc_mac(cspan_uint8_t, cspan_uint8_t, span_uint8_t) noexcept
        {
            return false;
        }
        [[nodiscard]] bool fail_ctr(cspan_uint8_t, cspan_uint8_t, span_uint8_t, span_uint8_t) noexcept
        {
            return false;
        }
        [[nodiscard]] bool fail_cbc_decrypt(cspan_uint8_t, cspan_uint8_t, cspan_uint8_t, span_uint8_t) noexcept
        {
            return false;
        }
        [[nodiscard]] bool fail_sha256(cspan_uint8_t, span_uint8_t) noexcept
        {
            return false;
        }
        [[nodiscard]] bool fail_pbkdf2(cspan_uint8_t, cspan_uint8_t, std::uint32_t, span_uint8_t) noexcept
        {
            return false;
        }
        [[nodiscard]] bool fail_random(span_uint8_t) noexcept
        {
            return false;
        }
        [[nodiscard]] bool fail_x25519_public(cspan_uint8_t, span_uint8_t) noexcept
        {
            return false;
        }
        [[nodiscard]] bool fail_x25519_derive(cspan_uint8_t, cspan_uint8_t, span_uint8_t) noexcept
        {
            return false;
        }

        /// @brief A backend whose every call fails, to reach the error branches above it.
        constexpr kd::crypto_backend failing_backend {
            .cbc_mac = &fail_cbc_mac,
            .ctr = &fail_ctr,
            .cbc_decrypt = &fail_cbc_decrypt,
            .sha256 = &fail_sha256,
            .pbkdf2_sha256 = &fail_pbkdf2,
            .random = &fail_random,
            .x25519_public = &fail_x25519_public,
            .x25519_derive = &fail_x25519_derive,
        };

        /// @brief The AN159 routing indication example: key, fields, associated data and plain payload.
        struct routing_example
        {
            ks::secret_key key {fixed<16u>("00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f")};
            ks::sequence_information_t timer {0xC0u, 0xC1u, 0xC2u, 0xC3u, 0xC4u, 0xC5u};
            ks::serial_number_t serial {0x00u, 0xFAu, 0x12u, 0x34u, 0x56u, 0x78u};
            ks::message_tag_t tag {0xAFu, 0xFEu};
            std::vector<std::uint8_t> associated {hex("06 10 09 50 00 37 00 00")};
            std::vector<std::uint8_t> payload {hex("06 10 05 30 00 11 29 00 bc d0 11 59 0a de 01 00 81")};

            [[nodiscard]] kd::block_t block_0() const noexcept
            {
                return kd::wrapper_block_0(timer, serial, tag, static_cast<std::uint16_t>(payload.size()));
            }
            [[nodiscard]] kd::block_t counter_0() const noexcept { return kd::wrapper_counter_0(timer, serial, tag); }
        };

        /// @brief Opens a sealed example after one alteration, and reports whether it was refused and wiped.
        /// @param example The example, sealed: its payload holds ciphertext.
        /// @param mac The sealed MAC.
        /// @param alter Which input to alter: 0 associated data, 1 payload, 2 MAC, 3 B0, 4 counter block.
        [[nodiscard]] bool refused_after_alteration(const routing_example& example, kd::block_t mac, const int alter) noexcept(false)
        {
            auto associated = example.associated;
            auto payload = example.payload;
            auto block_0 = example.block_0();
            auto counter_0 = example.counter_0();
            associated[3u] ^= (alter == 0) ? 0x01u : 0x00u;
            payload[5u] ^= (alter == 1) ? 0x01u : 0x00u;
            mac[7u] ^= (alter == 2) ? 0x01u : 0x00u;
            block_0[5u] ^= (alter == 3) ? 0x01u : 0x00u;
            counter_0[5u] ^= (alter == 4) ? 0x01u : 0x00u;

            const auto opened = kd::open(kd::evp_backend(), example.key, block_0, counter_0, associated, payload, mac);
            const auto wiped = std::all_of(payload.begin(), payload.end(), [](const std::uint8_t octet) noexcept { return octet == 0u; });
            return !opened.has_value() && (opened.error() == make_error_code(error::secure_authentication_failed)) && wiped;
        }

        /// @brief The xknx session fixture: fixed client key pair, server public key and the XOR of the two.
        struct session_fixture
        {
            ks::x25519_private_key client_private {fixed<32u>("b8 fa bd 62 66 5d 8b 9e 8a 9d 8b 1f 4b ca 42 c8"
                                                              "c2 78 9a 61 10 f5 0e 9d d7 85 b3 ed e8 83 f3 78")};
            ks::x25519_public_key_t client_public {fixed<32u>("0a a2 27 b4 fd 7a 32 31 9b a9 96 0a c0 36 ce 0e"
                                                              "5c 45 07 b5 ae 55 16 1f 10 78 b1 dc fb 3c b6 31")};
            ks::x25519_public_key_t server_public {fixed<32u>("bd f0 99 90 99 23 14 3e f0 a5 de 0b 3b e3 68 7b"
                                                              "c5 bd 3c f5 f9 e6 f9 01 69 9c d8 70 ec 1f f8 24")};

            [[nodiscard]] ks::x25519_public_key_t keys_xor() const noexcept
            {
                ks::x25519_public_key_t result {};
                for (std::size_t index {}; index < result.size(); ++index)
                    result[index] = client_public[index] ^ server_public[index];
                return result;
            }
        };

        /// @brief Computes a handshake MAC: CBC-MAC with a zero B0, then CTR under the handshake counter block.
        [[nodiscard]] kd::mac_result_t handshake_mac(const ks::secret_key& key, const cspan_uint8_t associated) noexcept
        {
            auto mac = kd::cbc_mac(kd::evp_backend(), key, kd::block_t {}, associated, {});
            if (!mac.has_value())
                return mac;
            if (const auto encrypted = kd::ctr(kd::evp_backend(), key, kd::handshake_counter_0(), *mac, {}); !encrypted.has_value())
                return std::unexpected(encrypted.error());
            return mac;
        }
    } // namespace detail

    TEST_CASE("knx secure backend reproduces the FIPS and RFC known answers", "[knx][secure][crypto][unit]")
    {
        const auto& backend = kd::evp_backend();

        // FIPS-197 appendix C.1: CBC with a zero IV over a single block is the block cipher itself.
        const auto aes_key = detail::hex("000102030405060708090a0b0c0d0e0f");
        const auto aes_plain = detail::hex("00112233445566778899aabbccddeeff");
        kd::block_t aes_cipher {};
        REQUIRE(backend.cbc_mac(aes_key, aes_plain, aes_cipher));
        CHECK(detail::equal(aes_cipher, "69c4e0d86a7b0430d8cdb78070b4c55a"));

        // NIST SHA-256 example "abc".
        std::array<std::uint8_t, kd::sha256_size> digest {};
        REQUIRE(backend.sha256(detail::octets("abc"), digest));
        CHECK(detail::equal(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

        // RFC 7914 section 11, both PBKDF2-HMAC-SHA256 vectors.
        std::array<std::uint8_t, 64u> derived {};
        REQUIRE(backend.pbkdf2_sha256(detail::octets("passwd"), detail::octets("salt"), 1u, derived));
        CHECK(detail::equal(derived, "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
                                     "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783"));
        REQUIRE(backend.pbkdf2_sha256(detail::octets("Password"), detail::octets("NaCl"), 80000u, derived));
        CHECK(detail::equal(derived, "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56"
                                     "a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d"));
    }

    TEST_CASE("knx secure backend reproduces the RFC 7748 X25519 exchange", "[knx][secure][crypto][unit]")
    {
        const auto& backend = kd::evp_backend();
        const auto alice_private = detail::hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
        const auto bob_private = detail::hex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
        std::array<std::uint8_t, ks::x25519_key_size> alice_public {};
        std::array<std::uint8_t, ks::x25519_key_size> bob_public {};
        REQUIRE(backend.x25519_public(alice_private, alice_public));
        REQUIRE(backend.x25519_public(bob_private, bob_public));
        CHECK(detail::equal(alice_public, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a"));
        CHECK(detail::equal(bob_public, "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f"));

        std::array<std::uint8_t, ks::x25519_key_size> shared {};
        REQUIRE(backend.x25519_derive(alice_private, bob_public, shared));
        CHECK(detail::equal(shared, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742"));

        // A low-order peer key agrees on nothing, and is refused rather than yielding an all-zero secret.
        const std::array<std::uint8_t, ks::x25519_key_size> low_order {};
        CHECK(!backend.x25519_derive(alice_private, low_order, shared));
    }

    TEST_CASE("knx secure composition reproduces the AN159 routing example", "[knx][secure][crypto][unit]")
    {
        detail::routing_example example {};
        const auto& backend = kd::evp_backend();
        CHECK(detail::equal(example.block_0(), "c0 c1 c2 c3 c4 c5 00 fa 12 34 56 78 af fe 00 11"));
        CHECK(detail::equal(example.counter_0(), "c0 c1 c2 c3 c4 c5 00 fa 12 34 56 78 af fe ff 00"));

        const auto plain_mac = kd::cbc_mac(backend, example.key, example.block_0(), example.associated, example.payload);
        REQUIRE(plain_mac.has_value());
        CHECK(detail::equal(*plain_mac, "bd 0a 29 4b 95 25 54 b2 35 39 20 4c 22 71 d2 6b"));

        const auto mac = kd::seal(backend, example.key, example.block_0(), example.counter_0(), example.associated, example.payload);
        REQUIRE(mac.has_value());
        CHECK(detail::equal(example.payload, "b7 ee 7e 8a 1c 2f 7b ba be c7 75 fd 6e 10 d0 bc 4b"));
        CHECK(detail::equal(*mac, "72 12 a0 3a aa e4 9d a8 56 89 77 4c 1d 2b 4d a4"));

        REQUIRE(
            kd::open(backend, example.key, example.block_0(), example.counter_0(), example.associated, example.payload, *mac).has_value());
        CHECK(detail::equal(example.payload, "06 10 05 30 00 11 29 00 bc d0 11 59 0a de 01 00 81"));
    }

    TEST_CASE("knx secure composition reproduces the AN159 session response MAC", "[knx][secure][crypto][unit]")
    {
        const auto device_code = ks::derive_device_authentication_code("trustme");
        REQUIRE(device_code.has_value());
        const auto associated = detail::hex("06 10 09 52 00 38 00 01 b7 52 be 24 64 59 26 0f 6b 0c 48 01 fb d5 a6 75"
                                            "99 f8 3b 40 57 b3 ef 1e 79 e4 69 ac 17 23 4e 15");
        const auto mac = kd::cbc_mac(kd::evp_backend(), *device_code, kd::block_t {}, associated, {});
        REQUIRE(mac.has_value());
        CHECK(detail::equal(*mac, "da 3d c6 af 79 89 6a a6 ee 75 73 d6 99 50 c2 83"));
    }

    TEST_CASE("knx secure derivations reproduce the KNX password keys", "[knx][secure][crypto][unit]")
    {
        const auto device_code = ks::derive_device_authentication_code("trustme");
        const auto user_key = ks::derive_user_password_key("secret");
        const auto keyring_hash = ks::derive_keyring_password_hash("pwd");
        REQUIRE(device_code.has_value());
        REQUIRE(user_key.has_value());
        REQUIRE(keyring_hash.has_value());
        CHECK(detail::equal(device_code->bytes(), "e1 58 e4 01 20 47 bd 6c c4 1a af bc 5c 04 c1 fc"));
        CHECK(detail::equal(user_key->bytes(), "03 fc ed b6 66 60 25 1e c8 1a 1a 71 69 01 69 6a"));
        CHECK(detail::equal(keyring_hash->bytes(), "c9 59 6c 2a 17 8d 79 86 83 d2 9a a4 52 6d 60 99"));
    }

    TEST_CASE("knx secure composition reproduces the xknx handshake MACs", "[knx][secure][crypto][unit]")
    {
        const detail::session_fixture fixture {};
        const auto client_public = ks::derive_x25519_public_key(fixture.client_private);
        REQUIRE(client_public.has_value());
        CHECK(*client_public == fixture.client_public);

        const auto keys_xor = fixture.keys_xor();
        const auto device_code = ks::derive_device_authentication_code("trustme");
        REQUIRE(device_code.has_value());
        const auto response_mac = detail::handshake_mac(*device_code, detail::join(detail::hex("06 10 09 52 00 38 00 01"), keys_xor));
        REQUIRE(response_mac.has_value());
        CHECK(detail::equal(*response_mac, "a9 22 50 5a aa 43 61 63 57 0b d5 49 4c 2d f2 a3"));

        const auto user_key = ks::derive_user_password_key("secret");
        REQUIRE(user_key.has_value());
        const auto authenticate_mac = detail::handshake_mac(*user_key, detail::join(detail::hex("06 10 09 53 00 18 00 01"), keys_xor));
        REQUIRE(authenticate_mac.has_value());
        CHECK(detail::equal(*authenticate_mac, "1f 1d 59 ea 9f 12 a1 52 e5 d9 72 7f 08 46 2c de"));
    }

    TEST_CASE("knx secure composition reproduces the xknx session wrappers", "[knx][secure][crypto][unit]")
    {
        const detail::session_fixture fixture {};
        const auto& backend = kd::evp_backend();
        const auto session_key = kd::derive_session_key(backend, fixture.client_private, fixture.server_public);
        REQUIRE(session_key.has_value());
        CHECK(detail::equal(session_key->bytes(), "28 94 26 c2 91 25 35 ba 98 27 9a 4d 18 43 c4 87"));

        // The client's SESSION_AUTHENTICATE, wrapped under the new key as the first frame of the session.
        const ks::sequence_information_t first {};
        const ks::serial_number_t client_serial {0x00u, 0xFAu, 0x12u, 0x34u, 0x56u, 0x78u};
        const ks::message_tag_t tag {0xAFu, 0xFEu};
        auto authenticate = detail::hex("06 10 09 53 00 18 00 01 1f 1d 59 ea 9f 12 a1 52 e5 d9 72 7f 08 46 2c de");
        const auto wrapped_mac =
            kd::seal(backend, *session_key, kd::wrapper_block_0(first, client_serial, tag, 24u),
                     kd::wrapper_counter_0(first, client_serial, tag), detail::hex("06 10 09 50 00 3e 00 01"), authenticate);
        REQUIRE(wrapped_mac.has_value());
        CHECK(detail::equal(authenticate, "79 15 a4 f3 6e 6e 42 08 d2 8b 4a 20 7d 8f 35 c0 d1 38 c2 6a 7b 5e 71 69"));
        CHECK(detail::equal(*wrapped_mac, "52 db a8 e7 e4 bd 80 bd 7d 86 8a 3a e7 87 49 de"));

        // The server's SESSION_STATUS, wrapped under a serial number of its own: authentication succeeded.
        const ks::serial_number_t server_serial {0x00u, 0xFAu, 0xAAu, 0xAAu, 0xAAu, 0xAAu};
        auto status = detail::hex("26 15 6d b5 c7 49 88 8f");
        REQUIRE(kd::open(backend, *session_key, kd::wrapper_block_0(first, server_serial, tag, 8u),
                         kd::wrapper_counter_0(first, server_serial, tag), detail::hex("06 10 09 50 00 2e 00 01"), status,
                         detail::fixed<16u>("a3 73 c3 e0 b4 bd e4 49 7c 39 5e 4b 1c 2f 46 a1"))
                    .has_value());
        CHECK(detail::equal(status, "06 10 09 54 00 08 00 00"));
    }

    TEST_CASE("knx secure open reports every alteration the same way and wipes the payload", "[knx][secure][crypto][unit]")
    {
        detail::routing_example example {};
        const auto mac =
            kd::seal(kd::evp_backend(), example.key, example.block_0(), example.counter_0(), example.associated, example.payload);
        REQUIRE(mac.has_value());
        for (int alteration {}; alteration < 5; ++alteration)
        {
            INFO("alteration=" << alteration);
            CHECK(detail::refused_after_alteration(example, *mac, alteration));
        }
    }

    TEST_CASE("knx secure composition reports backend failures as crypto_failure", "[knx][secure][crypto][unit]")
    {
        detail::routing_example example {};
        const auto failure = make_error_code(error::crypto_failure);
        CHECK(kd::cbc_mac(detail::failing_backend, example.key, {}, example.associated, example.payload).error() == failure);
        CHECK(kd::seal(detail::failing_backend, example.key, {}, {}, example.associated, example.payload).error() == failure);
        CHECK(kd::open(detail::failing_backend, example.key, {}, {}, example.associated, example.payload, {}).error() == failure);
        CHECK(kd::derive_password_key(detail::failing_backend, "secret", "salt").error() == failure);

        const detail::session_fixture fixture {};
        CHECK(kd::derive_session_key(detail::failing_backend, fixture.client_private, fixture.server_public).error() == failure);

        // A MAC that cannot be computed after decryption still leaves no plaintext behind.
        auto decrypting_only = kd::evp_backend();
        decrypting_only.cbc_mac = &detail::fail_cbc_mac;
        CHECK(kd::open(decrypting_only, example.key, {}, {}, example.associated, example.payload, {}).error() == failure);
        CHECK(std::all_of(example.payload.begin(), example.payload.end(), [](const std::uint8_t octet) noexcept { return octet == 0u; }));

        // An input beyond the bound is refused as a length, not truncated.
        const std::vector<std::uint8_t> oversized(kd::max_mac_input_size, 0u);
        CHECK(kd::cbc_mac(kd::evp_backend(), example.key, {}, {}, oversized).error() == make_error_code(error::invalid_length));
    }

    TEST_CASE("knx secret material is wiped when moved from or cleared", "[knx][secure][crypto][unit]")
    {
        ks::secret_key original {detail::fixed<16u>("00 11 22 33 44 55 66 77 88 99 aa bb cc dd ee ff")};
        const auto copy = original.clone();
        ks::secret_key moved {std::move(original)};
        CHECK(original.empty()); // NOLINT(bugprone-use-after-move): the moved-from state is what is under test.
        CHECK(std::ranges::equal(moved.bytes(), copy.bytes()));
        moved.clear();
        CHECK(moved.empty());

        ks::secret_string password {"trustme"};
        ks::secret_string taken {std::move(password)};
        CHECK(password.empty()); // NOLINT(bugprone-use-after-move): as above.
        CHECK(taken.view() == "trustme");
    }

    TEST_CASE("knx system entropy produces distinct key pairs that agree", "[knx][secure][crypto][unit]")
    {
        auto& entropy = ks::system_entropy();
        auto first = entropy.generate_key_pair();
        auto second = entropy.generate_key_pair();
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        CHECK(first->public_key != second->public_key);

        const auto& backend = kd::evp_backend();
        const auto first_key = kd::derive_session_key(backend, first->private_key, second->public_key);
        const auto second_key = kd::derive_session_key(backend, second->private_key, first->public_key);
        REQUIRE(first_key.has_value());
        REQUIRE(second_key.has_value());
        CHECK(std::ranges::equal(first_key->bytes(), second_key->bytes()));

        std::array<std::uint8_t, 32u> random {};
        REQUIRE(entropy.fill(random).has_value());
        CHECK(!std::all_of(random.begin(), random.end(), [](const std::uint8_t octet) noexcept { return octet == 0u; }));
    }

    TEST_CASE("knx secure sequence fields and serial numbers", "[knx][secure][crypto][unit]")
    {
        STATIC_CHECK(ks::decode_sequence(ks::encode_sequence(0xC0C1C2C3C4C5ull)) == 0xC0C1C2C3C4C5ull);
        STATIC_CHECK(ks::encode_sequence(0x0000000000000102ull) == ks::sequence_information_t {0u, 0u, 0u, 0u, 0x01u, 0x02u});
        STATIC_CHECK(ks::decode_sequence(ks::encode_sequence(ks::max_sequence)) == ks::max_sequence);
        STATIC_CHECK(!ks::valid_serial_number(ks::serial_number_t {}));
        STATIC_CHECK(ks::valid_serial_number(ks::serial_number_t {0x00u, 0xFAu, 0u, 0u, 0u, 0u}));
        CHECK(kd::constant_time_equal(detail::hex("0102"), detail::hex("0102")));
        CHECK(!kd::constant_time_equal(detail::hex("0102"), detail::hex("0103")));
        CHECK(!kd::constant_time_equal(detail::hex("0102"), detail::hex("010203")));
    }
}
