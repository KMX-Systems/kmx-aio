/// @file src/kmx/aio/knx/data_secure_test.cpp
/// @brief KNX Data Secure group communication: xknx's vectors, and the policy a context applies to whole frames.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details The vectors come out of xknx 3.20.0's own Data Secure code, through
/// script/feature/knx/secure-vectors/generate.py: both algorithms, group and individual destinations, standard and
/// extended frames. Around them: keys and senders, sequence numbers, the services that are refused, reservation of
/// sequence numbers before use, and unencrypted traffic to a secured group.
#include <kmx/aio/knx/data_secure.hpp>
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/data_secure/context.hpp>
    #include <kmx/aio/knx/data_secure/sequence_store.hpp>
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/group_address.hpp>
    #include <kmx/aio/knx/individual_address.hpp>
    #include <kmx/aio/knx/keyring.hpp>
    #include <kmx/aio/knx/keyring/document.hpp>
    #include <kmx/aio/test/knx/secure_vectors.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <algorithm>
    #include <cstddef>
    #include <cstdint>
    #include <expected>
    #include <fstream>
    #include <iterator>
    #include <span>
    #include <string>
    #include <string_view>
    #include <system_error>
    #include <vector>
#endif

namespace kmx::aio::test::knx::data_secure_test
{
    namespace kn = kmx::aio::knx;
    namespace ds = kmx::aio::knx::data_secure;
    namespace kr = kmx::aio::knx::keyring;
    namespace ks = kmx::aio::knx::secure;
    namespace sv = kmx::aio::test::knx::secure_vectors;
    using kn::error;
    using kn::make_error_code;

    namespace detail
    {
        /// @brief The group key of 1/2/3 in these tests: the first group key of the vendored DataSecure_usb.knxkeys.
        constexpr std::string_view group_key_text = "d2effbdc88eb5c9a57a73b0aad64996f";
        /// @brief An L_Data.ind from 1.1.5 switching 1/2/3 on.
        constexpr std::string_view switch_on = "2900bce011050a03010081";
        /// @brief The same telegram with no source address.
        constexpr std::string_view switch_on_without_source = "2900bce000000a03010081";
        /// @brief An L_Data.ind from 1.1.5 switching 1/2/4 on, a group without a key.
        constexpr std::string_view switch_on_elsewhere = "2900bce011050a04010081";

        /// @brief Formats octets as lower-case hexadecimal, for failure messages.
        [[nodiscard]] std::string hex_text(const cspan_uint8_t octets)
        {
            static constexpr std::string_view digits = "0123456789abcdef";
            std::string text {};
            for (const auto octet: octets)
            {
                text.push_back(digits[octet >> 4u]);
                text.push_back(digits[octet & 0x0Fu]);
            }

            return text;
        }

        /// @brief One generated Data Secure vector.
        struct vector_row
        {
            ds::algorithm algorithm {};
            sv::octets_t key {};
            std::uint64_t sequence {};
            sv::octets_t plain {};
            sv::octets_t secured {};
        };

        [[nodiscard]] std::vector<vector_row> vectors()
        {
            std::ifstream input(sv::conformance_directory() / "data-secure-vectors.tsv");
            std::vector<vector_row> result {};
            for (std::string line; std::getline(input, line);)
            {
                const auto fields = line.starts_with('#') ? std::vector<std::string> {} : sv::split_tabs(line);
                if ((fields.size() != 6u) || (fields[0u] != "data_secure"))
                    continue;
                const auto algorithm =
                    (fields[1u] == "authentication_only") ? ds::algorithm::authentication_only : ds::algorithm::authenticated_encryption;
                result.push_back(vector_row {algorithm, sv::hex(fields[2u]), std::stoull(fields[3u], nullptr, 16), sv::hex(fields[4u]),
                                             sv::hex(fields[5u])});
            }

            return result;
        }

        /// @brief Where the link header of a cEMI frame starts.
        [[nodiscard]] std::size_t link_of(const sv::octets_t& frame) noexcept
        {
            return 2u + frame[1u];
        }

        [[nodiscard]] ds::frame_binding binding_of(const sv::octets_t& frame) noexcept
        {
            const auto link = link_of(frame);
            return ds::frame_binding {.source =
                                          kn::individual_address {static_cast<std::uint16_t>((frame[link + 2u] << 8u) | frame[link + 3u])},
                                      .destination = static_cast<std::uint16_t>((frame[link + 4u] << 8u) | frame[link + 5u]),
                                      .control_field_2 = frame[link + 1u],
                                      .transport_control = static_cast<std::uint8_t>(frame[link + 7u] & 0xFCu)};
        }

        /// @brief The plain APDU of a frame, with the transport control bits cleared.
        [[nodiscard]] sv::octets_t plain_apdu_of(const sv::octets_t& frame)
        {
            const auto link = link_of(frame);
            sv::octets_t apdu(frame.begin() + static_cast<std::ptrdiff_t>(link + 7u), frame.end());
            apdu[0u] = static_cast<std::uint8_t>(apdu[0u] & 0x03u);
            return apdu;
        }

        /// @brief What follows the APCI of a secured frame.
        [[nodiscard]] sv::octets_t secured_octets_of(const sv::octets_t& frame)
        {
            return sv::octets_t(frame.begin() + static_cast<std::ptrdiff_t>(link_of(frame) + 9u), frame.end());
        }

        /// @brief A sequence store in memory, recording every reservation.
        class memory_store final: public ds::sequence_store
        {
        public:
            explicit memory_store(const std::uint64_t first) noexcept: first_unreserved(first) {}

            std::uint64_t first_unreserved {};
            std::vector<std::uint64_t> reservations {};
            bool failing {};

            [[nodiscard]] std::expected<std::uint64_t, std::error_code> load() noexcept override { return first_unreserved; }

            [[nodiscard]] expected_void_t reserve_until(const std::uint64_t limit) noexcept override
            {
                if (failing)
                    return std::unexpected(std::make_error_code(std::errc::io_error));
                reservations.push_back(limit);
                first_unreserved = limit;
                return {};
            }
        };

        template <typename Value>
        [[nodiscard]] std::error_code error_of(const expected_t<Value>& result) noexcept
        {
            return result.has_value() ? std::error_code {} : result.error();
        }

        /// @brief One group, the key it is secured under, and the one sender it trusts.
        struct configuration_params
        {
            /// @brief The group key.
            sv::octets_t key {};
            /// @brief The group.
            std::uint16_t group {};
            /// @brief The sender trusted.
            kn::individual_address sender {};
            /// @brief The last sequence number accepted from the sender.
            std::uint64_t last_sequence {};
            /// @brief How outgoing telegrams are protected.
            ds::algorithm outgoing {ds::algorithm::authenticated_encryption};
        };

        /// @brief A configuration keying one group, trusting its sender from just past the last sequence number.
        [[nodiscard]] ds::configuration configuration(const configuration_params& params)
        {
            ds::configuration value {};
            value.group_keys.push_back(kr::group_key {.address = kn::group_address {params.group}, .key = sv::key(params.key)});
            value.senders.push_back(ds::sender_sequence {.address = params.sender, .last_valid_sequence = params.last_sequence});
            value.outgoing = params.outgoing;
            return value;
        }

        [[nodiscard]] ds::configuration switch_configuration(const std::uint64_t last_sequence = 0u)
        {
            return configuration({.key = sv::hex(group_key_text),
                                  .group = 0x0A03u,
                                  .sender = kn::individual_address {1u, 1u, 5u},
                                  .last_sequence = last_sequence});
        }

        /// @brief Secures @p frame under a sender whose sequence numbers start at @p first.
        [[nodiscard]] std::vector<sv::octets_t> secured_run(const sv::octets_t& frame, const std::uint64_t first, const std::size_t count)
        {
            memory_store store {first};
            ds::context sender {switch_configuration(), &store};
            std::vector<sv::octets_t> frames {};
            for (std::size_t index {}; index < count; ++index)
            {
                auto secured = sender.secure_frame(frame);
                REQUIRE(secured.has_value());
                frames.push_back(std::move(*secured));
            }

            return frames;
        }

        [[nodiscard]] std::string read_fixture(const std::string_view name)
        {
            std::ifstream file(sv::conformance_directory() / "keyrings" / std::string {name}, std::ios::binary);
            return std::string {std::istreambuf_iterator<char> {file}, std::istreambuf_iterator<char> {}};
        }
    }

    TEST_CASE("knx data secure seals and opens xknx's vectors", "[knx][data_secure][unit]")
    {
        const auto rows = detail::vectors();
        REQUIRE(rows.size() == 6u);
        for (const auto& row: rows)
        {
            INFO("secured " << detail::hex_text(row.secured));
            const auto binding = detail::binding_of(row.plain);
            const auto plain_apdu = detail::plain_apdu_of(row.plain);
            const auto key = sv::key(row.key);
            sv::octets_t sealed(plain_apdu.size() + ds::secured_apdu_overhead, 0u);
            const auto size = ds::seal_apdu(
                sealed, key, ds::apdu_fields {.control = {.algorithm = row.algorithm}, .sequence = row.sequence, .binding = binding},
                plain_apdu);
            REQUIRE(size.has_value());
            CHECK(sealed == detail::secured_octets_of(row.secured));

            sv::octets_t opened(plain_apdu.size(), 0u);
            const auto recovered = ds::open_apdu(opened, key, detail::binding_of(row.secured), detail::secured_octets_of(row.secured));
            REQUIRE(recovered.has_value());
            CHECK(opened == plain_apdu);
            CHECK(ds::sequence_of(detail::secured_octets_of(row.secured)) == row.sequence);
        }
    }

    TEST_CASE("knx data secure context secures and opens xknx's group telegrams", "[knx][data_secure][unit]")
    {
        for (const auto& row: detail::vectors())
        {
            const auto binding = detail::binding_of(row.plain);
            if ((row.plain[detail::link_of(row.plain) + 1u] & 0x80u) == 0u)
                continue;
            INFO("secured " << detail::hex_text(row.secured));
            detail::memory_store store {row.sequence};
            ds::context sender {
                detail::configuration({.key = row.key, .group = binding.destination, .sender = binding.source, .outgoing = row.algorithm}),
                &store};
            const auto secured = sender.secure_frame(row.plain);
            REQUIRE(secured.has_value());
            CHECK(*secured == row.secured);

            ds::context receiver {detail::configuration(
                {.key = row.key, .group = binding.destination, .sender = binding.source, .last_sequence = row.sequence - 1u})};
            const auto opened = receiver.open_frame(row.secured);
            REQUIRE(opened.has_value());
            CHECK(*opened == row.plain);
        }
    }

    TEST_CASE("knx data secure refuses unknown groups, unknown senders and senders not allowed", "[knx][data_secure][unit]")
    {
        const auto secured = detail::secured_run(sv::hex(detail::switch_on), 7u, 1u).front();

        ds::context other_group {detail::configuration(
            {.key = sv::hex(detail::group_key_text), .group = 0x0A04u, .sender = kn::individual_address {1u, 1u, 5u}})};
        CHECK(detail::error_of(other_group.open_frame(secured)) == make_error_code(error::secure_key_missing));

        ds::context other_sender {detail::configuration(
            {.key = sv::hex(detail::group_key_text), .group = 0x0A03u, .sender = kn::individual_address {1u, 1u, 9u}})};
        CHECK(detail::error_of(other_sender.open_frame(secured)) == make_error_code(error::secure_key_missing));

        auto restricted = detail::switch_configuration();
        restricted.allowed_senders.push_back(
            kr::group_senders {.address = kn::group_address {0x0A03u}, .senders = {kn::individual_address {1u, 1u, 6u}}});
        ds::context not_allowed {std::move(restricted)};
        CHECK(detail::error_of(not_allowed.open_frame(secured)) == make_error_code(error::secure_key_missing));

        CHECK(other_group.counters().missing_keys == 1u);
        CHECK(other_sender.counters().missing_keys == 1u);
        CHECK(not_allowed.counters().missing_keys == 1u);
    }

    TEST_CASE("knx data secure refuses stale sequence numbers, and a forged MAC changes nothing", "[knx][data_secure][unit]")
    {
        const auto frames = detail::secured_run(sv::hex(detail::switch_on), 10u, 3u);
        ds::context receiver {detail::switch_configuration()};

        CHECK(receiver.open_frame(frames[1u]).has_value());
        // Delivered out of order, and delivered twice: both are refused and counted.
        CHECK(detail::error_of(receiver.open_frame(frames[0u])) == make_error_code(error::secure_replay));
        CHECK(detail::error_of(receiver.open_frame(frames[1u])) == make_error_code(error::secure_replay));
        CHECK(receiver.counters().replays == 2u);

        // A forged MAC under a fresh sequence number leaves the sender table as it was, so the genuine frame still opens (P2).
        auto forged = frames[2u];
        forged.back() ^= 0x01u;
        CHECK(detail::error_of(receiver.open_frame(forged)) == make_error_code(error::secure_authentication_failed));
        CHECK(receiver.counters().authentication_failures == 1u);
        CHECK(receiver.open_frame(frames[2u]).has_value());
    }

    TEST_CASE("knx data secure refuses S-A_Sync, tool access, system broadcast and point-to-point", "[knx][data_secure][unit]")
    {
        const auto secured = detail::secured_run(sv::hex(detail::switch_on), 20u, 1u).front();
        const auto control = detail::link_of(secured) + 9u;
        ds::context receiver {detail::switch_configuration()};
        for (const std::uint8_t octet:
             {std::uint8_t {0x12u}, std::uint8_t {0x13u}, std::uint8_t {0x90u}, std::uint8_t {0x18u}, std::uint8_t {0x30u}})
        {
            auto altered = secured;
            altered[control] = octet;
            CHECK(detail::error_of(receiver.open_frame(altered)) == make_error_code(error::secure_unsupported));
        }

        CHECK(receiver.counters().refused_services == 5u);

        // Point-to-point Data Secure is refused on the way in, and point-to-point traffic goes out as it is.
        const auto rows = detail::vectors();
        const auto point_to_point = std::ranges::find_if(rows, [](const detail::vector_row& row)
                                                         { return (row.plain[detail::link_of(row.plain) + 1u] & 0x80u) == 0u; });
        REQUIRE(point_to_point != rows.end());
        CHECK(detail::error_of(receiver.open_frame(point_to_point->secured)) == make_error_code(error::secure_unsupported));
        const auto unchanged = receiver.secure_frame(point_to_point->plain);
        REQUIRE(unchanged.has_value());
        CHECK(*unchanged == point_to_point->plain);
    }

    TEST_CASE("knx data secure reserves sequence numbers before it sends them", "[knx][data_secure][unit]")
    {
        const auto plain = sv::hex(detail::switch_on);
        detail::memory_store store {100u};
        auto configuration = detail::switch_configuration();
        configuration.reservation_block = 4u;
        {
            ds::context sender {std::move(configuration), &store};
            REQUIRE(sender.secure_frame(plain).has_value());
            // The first block is recorded before the first number in it goes out.
            REQUIRE(store.reservations == std::vector<std::uint64_t> {104u});
            for (std::uint64_t expected = 101u; expected < 106u; ++expected)
            {
                const auto secured = sender.secure_frame(plain);
                REQUIRE(secured.has_value());
                CHECK(ds::sequence_of(detail::secured_octets_of(*secured)) == expected);
            }

            CHECK(store.reservations == std::vector<std::uint64_t> {104u, 108u});
        }

        // After a restart the sender resumes past everything it reserved, so no number it may have sent is sent again.
        auto restarted_configuration = detail::switch_configuration();
        restarted_configuration.reservation_block = 4u;
        ds::context restarted {std::move(restarted_configuration), &store};
        const auto first = restarted.secure_frame(plain);
        REQUIRE(first.has_value());
        CHECK(ds::sequence_of(detail::secured_octets_of(*first)) == 108u);

        // A block that cannot be recorded is not used: nothing is sent under it.
        store.failing = true;
        for (std::size_t index {}; index < 3u; ++index)
            REQUIRE(restarted.secure_frame(plain).has_value());
        CHECK(detail::error_of(restarted.secure_frame(plain)) == std::make_error_code(std::errc::io_error));
    }

    TEST_CASE("knx data secure refuses an unencrypted telegram to a secured group", "[knx][data_secure][unit]")
    {
        ds::context receiver {detail::switch_configuration()};
        CHECK(detail::error_of(receiver.open_frame(sv::hex(detail::switch_on))) == make_error_code(error::secure_frame_required));
        CHECK(receiver.counters().unencrypted_refused == 1u);
        CHECK(receiver.secured_group(kn::group_address {0x0A03u}));
        // A frame with no APCI to the secured group has nothing to secure, and is refused rather than sent in the clear.
        CHECK(detail::error_of(receiver.secure_frame(sv::hex("2900bce011050a030000"))) == make_error_code(error::secure_unsupported));
        // A frame with data_length == 0 is safely handled without out-of-bounds reads.
        const auto control_frame = sv::hex("2900bce011050a030000");
        CHECK(detail::error_of(receiver.open_frame(control_frame)) == make_error_code(error::secure_frame_required));
        CHECK(receiver.counters().unencrypted_refused == 2u);

        // A compact APDU whose payload octet happens to match A_SecureService's low octet is not misidentified as secured.
        const auto compact_secured_candidate = sv::hex("2900bce011050a030103f1");
        CHECK(detail::error_of(receiver.open_frame(compact_secured_candidate)) == make_error_code(error::secure_frame_required));
        CHECK(receiver.counters().unencrypted_refused == 3u);

        const auto compact_elsewhere = sv::hex("2900bce011050a040103f1");
        const auto received_compact = receiver.open_frame(compact_elsewhere);
        REQUIRE(received_compact.has_value());
        CHECK(*received_compact == compact_elsewhere);

        // A group without a key is not Data Secure's business, in either direction.
        const auto elsewhere = sv::hex(detail::switch_on_elsewhere);
        const auto received = receiver.open_frame(elsewhere);
        REQUIRE(received.has_value());
        CHECK(*received == elsewhere);
        const auto sent = receiver.secure_frame(elsewhere);
        REQUIRE(sent.has_value());
        CHECK(*sent == elsewhere);
        CHECK(!receiver.secured_group(kn::group_address {0x0A04u}));
    }

    TEST_CASE("knx data secure names the local address as source, and opens a confirmation of its own telegram", "[knx][data_secure][unit]")
    {
        const auto vector = detail::vectors().front();
        auto configuration = detail::switch_configuration();
        configuration.local_address = kn::individual_address {1u, 1u, 5u};
        detail::memory_store store {vector.sequence};
        ds::context sender {std::move(configuration), &store};
        const auto secured = sender.secure_frame(sv::hex(detail::switch_on_without_source));
        REQUIRE(secured.has_value());
        CHECK(*secured == vector.secured);

        ds::context anonymous {detail::switch_configuration()};
        CHECK(detail::error_of(anonymous.secure_frame(sv::hex(detail::switch_on_without_source))) ==
              make_error_code(error::invalid_configuration));

        // An L_Data.con carries back what this endpoint sent: it opens however often, with no sender table to consult.
        auto confirmation = vector.secured;
        confirmation[0u] = 0x2Eu;
        ds::context confirmer {detail::configuration(
            {.key = sv::hex(detail::group_key_text), .group = 0x0A03u, .sender = kn::individual_address {1u, 1u, 9u}})};
        for (std::size_t index {}; index < 2u; ++index)
        {
            const auto opened = confirmer.open_frame(confirmation);
            REQUIRE(opened.has_value());
            CHECK(opened->size() == vector.plain.size());
            CHECK(std::ranges::equal(std::span {*opened}.subspan(1u), std::span {vector.plain}.subspan(1u)));
        }
    }

    TEST_CASE("knx data secure builds its configuration from a keyring", "[knx][data_secure][keyring][unit]")
    {
        const auto keyring = kr::load(detail::read_fixture("DataSecure_only_one_interface.knxkeys"), "test");
        REQUIRE(keyring.has_value());
        const auto receiver = kn::individual_address {1u, 0u, 4u};
        const auto configuration = ds::configuration_for(*keyring, receiver);
        REQUIRE(configuration.has_value());
        CHECK(configuration->local_address == receiver);
        CHECK(configuration->group_keys.size() == 3u);
        const auto trusted = [&configuration](const kn::individual_address address)
        { return std::ranges::find(configuration->senders, address, &ds::sender_sequence::address) != configuration->senders.end(); };
        CHECK(trusted(kn::individual_address {1u, 0u, 1u}));
        CHECK(trusted(kn::individual_address {1u, 0u, 2u}));
        // A group the interface lists no senders for accepts any trusted sender; one with senders accepts only them.
        const auto& groups = keyring->interfaces.front().groups;
        CHECK(std::ranges::none_of(configuration->allowed_senders,
                                   [&groups](const kr::group_senders& value) { return value.address == groups[0u].address; }));
        CHECK(std::ranges::any_of(configuration->allowed_senders,
                                  [&groups](const kr::group_senders& value) { return value.address == groups[1u].address; }));

        // An endpoint the keyring has no interface for is keyed for every group, with no senders enforced.
        const auto elsewhere = ds::configuration_for(*keyring, kn::individual_address {9u, 9u, 9u});
        REQUIRE(elsewhere.has_value());
        CHECK(elsewhere->group_keys.size() == keyring->group_keys.size());
        CHECK(elsewhere->allowed_senders.empty());
        CHECK(detail::error_of(ds::configuration_for(kr::document {}, receiver)) == make_error_code(error::secure_key_missing));
    }
}
