/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/detail/secure_vectors.hpp>
#include <kmx/aio/knx/frame.hpp>
#include <kmx/aio/knx/secure.hpp>

#include <array>
#include <cstdint>
#include <system_error>
#include <vector>

namespace kmx::aio::test::knx::secure_vectors_test
{
    using namespace kmx::aio::knx;

    class passthrough_provider final: public secure::provider
    {
    public:
        [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::error_code> protect(
            const std::span<const std::uint8_t> packet, std::uint64_t /*sequence*/) noexcept override
        {
            return std::vector<std::uint8_t>(packet.begin(), packet.end());
        }

        [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::error_code> unprotect(
            const std::span<const std::uint8_t> packet, std::uint64_t /*sequence*/) noexcept override
        {
            return std::vector<std::uint8_t>(packet.begin(), packet.end());
        }
    };

    TEST_CASE("knx secure envelope encoder matches golden vectors", "[knx][secure][vectors][integration]")
    {
        {
            secure::packet ip_secure {
                .selected = secure::profile::ip_secure,
                .sequence = detail::secure_vectors::ip_secure_sequence,
                .payload = std::vector<std::uint8_t>(
                    detail::secure_vectors::ip_secure_payload.begin(), detail::secure_vectors::ip_secure_payload.end()),
            };

            std::array<std::uint8_t, detail::secure_vectors::ip_secure_wire.size()> encoded {};
            REQUIRE(secure::encode_secure_packet(encoded, ip_secure).has_value());
            CHECK(encoded == detail::secure_vectors::ip_secure_wire);
        }

        {
            secure::packet data_secure {
                .selected = secure::profile::data_secure,
                .sequence = detail::secure_vectors::data_secure_sequence,
                .payload = std::vector<std::uint8_t>(
                    detail::secure_vectors::data_secure_payload.begin(), detail::secure_vectors::data_secure_payload.end()),
            };

            std::array<std::uint8_t, detail::secure_vectors::data_secure_wire.size()> encoded {};
            REQUIRE(secure::encode_secure_packet(encoded, data_secure).has_value());
            CHECK(encoded == detail::secure_vectors::data_secure_wire);
        }
    }

    TEST_CASE("knx secure envelope decoder matches golden vectors", "[knx][secure][vectors][integration]")
    {
        const auto ip_secure = secure::decode_secure_packet(detail::secure_vectors::ip_secure_wire);
        REQUIRE(ip_secure.has_value());
        CHECK(ip_secure->selected == secure::profile::ip_secure);
        CHECK(ip_secure->sequence == detail::secure_vectors::ip_secure_sequence);
        CHECK(ip_secure->payload == std::vector<std::uint8_t>(
            detail::secure_vectors::ip_secure_payload.begin(), detail::secure_vectors::ip_secure_payload.end()));

        const auto data_secure = secure::decode_secure_packet(detail::secure_vectors::data_secure_wire);
        REQUIRE(data_secure.has_value());
        CHECK(data_secure->selected == secure::profile::data_secure);
        CHECK(data_secure->sequence == detail::secure_vectors::data_secure_sequence);
        CHECK(data_secure->payload == std::vector<std::uint8_t>(
            detail::secure_vectors::data_secure_payload.begin(), detail::secure_vectors::data_secure_payload.end()));
    }

    TEST_CASE("knx secure envelope decoder rejects malformed vectors", "[knx][secure][vectors][unit]")
    {
        {
            auto wrong_service = detail::secure_vectors::ip_secure_wire;
            wrong_service[2u] = 0x04u;
            wrong_service[3u] = 0x20u;
            const auto decoded = secure::decode_secure_packet(wrong_service);
            REQUIRE(!decoded.has_value());
            CHECK(decoded.error() == make_error_code(error::unsupported_service));
        }

        {
            auto wrong_reserved = detail::secure_vectors::ip_secure_wire;
            wrong_reserved[frame::communication_header_size + 1u] = 1u;
            const auto decoded = secure::decode_secure_packet(wrong_reserved);
            REQUIRE(!decoded.has_value());
            CHECK(decoded.error() == make_error_code(error::malformed_frame));
        }

        {
            auto wrong_profile = detail::secure_vectors::ip_secure_wire;
            wrong_profile[frame::communication_header_size] = 0x7Fu;
            const auto decoded = secure::decode_secure_packet(wrong_profile);
            REQUIRE(!decoded.has_value());
            CHECK(decoded.error() == make_error_code(error::unsupported_service));
        }

        {
            auto wrong_payload_length = detail::secure_vectors::ip_secure_wire;
            wrong_payload_length[frame::communication_header_size + 10u] = 0u;
            wrong_payload_length[frame::communication_header_size + 11u] = 4u;
            const auto decoded = secure::decode_secure_packet(wrong_payload_length);
            REQUIRE(!decoded.has_value());
            CHECK(decoded.error() == make_error_code(error::malformed_frame));
        }

        {
            auto wrong_total_length = detail::secure_vectors::ip_secure_wire;
            wrong_total_length[4u] = 0u;
            wrong_total_length[5u] = static_cast<std::uint8_t>(detail::secure_vectors::ip_secure_wire.size() - 1u);
            const auto decoded = secure::decode_secure_packet(wrong_total_length);
            REQUIRE(!decoded.has_value());
            CHECK(decoded.error() == make_error_code(error::malformed_frame));
        }
    }

    TEST_CASE("knx secure profile vectors run through provider and replay policies", "[knx][secure][vectors][unit]")
    {
        passthrough_provider provider {};
        secure::replay_window_state replay {32u};

        const auto protected_vector = secure::protect_packet(
            provider,
            secure::profile::ip_secure,
            detail::secure_vectors::ip_secure_payload,
            detail::secure_vectors::ip_secure_sequence);
        REQUIRE(protected_vector.has_value());
        CHECK(*protected_vector == std::vector<std::uint8_t>(
            detail::secure_vectors::ip_secure_wire.begin(), detail::secure_vectors::ip_secure_wire.end()));

        const auto first = secure::unprotect_packet(
            provider, secure::profile::ip_secure, *protected_vector, &replay);
        REQUIRE(first.has_value());
        CHECK(*first == std::vector<std::uint8_t>(
            detail::secure_vectors::ip_secure_payload.begin(), detail::secure_vectors::ip_secure_payload.end()));

        const auto duplicate = secure::unprotect_packet(
            provider, secure::profile::ip_secure, *protected_vector, &replay);
        REQUIRE(!duplicate.has_value());
        CHECK(duplicate.error() == make_error_code(error::sequence_error));

        const auto wrong_profile = secure::unprotect_packet(
            provider, secure::profile::data_secure, *protected_vector);
        REQUIRE(!wrong_profile.has_value());
        CHECK(wrong_profile.error() == make_error_code(error::invalid_configuration));
    }
}
