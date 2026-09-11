/// @file src/kmx/aio/knx/secure/routing_client_secure_test.cpp
/// @brief Unit tests for the KNX IP Secure routing client: timer synchronisation, wrapped frames and dropped forgeries.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#ifndef PCH
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/knx/datagram_transport.hpp>
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/routing.hpp>
    #include <kmx/aio/knx/routing/client.hpp>
    #include <kmx/aio/knx/secure/timer_notify.hpp>
    #include <kmx/aio/knx/secure/wrapper.hpp>
    #include <kmx/aio/test/knx/secure_vectors.hpp>
    #include <kmx/aio/test/knx/secure_vectors/scripted_entropy.hpp>
    #include <kmx/aio/test/knx/telegram.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <algorithm>
    #include <array>
    #include <cstdint>
    #include <deque>
    #include <limits>
    #include <optional>
    #include <span>
    #include <string_view>
    #include <variant>
    #include <vector>
    #include <netinet/in.h>
#endif

namespace kmx::aio::test::knx::secure::routing_client_secure_test
{
    namespace kn = kmx::aio::knx;
    namespace kr = kmx::aio::knx::routing;
    namespace ks = kmx::aio::knx::secure;
    namespace sv = kmx::aio::test::knx::secure_vectors;
    using kn::error;
    using kn::make_error_code;

    namespace detail
    {
        std::uint64_t now_ms {};

        [[nodiscard]] std::uint64_t clock() noexcept
        {
            return now_ms;
        }

        inline constexpr ks::serial_number_t own_serial {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u};
        inline constexpr ks::serial_number_t other_serial {0x00u, 0xFAu, 0x12u, 0x34u, 0x56u, 0x78u};
        inline constexpr std::uint64_t one_hour_ms = 3'600'000u;
        inline constexpr std::string_view backbone_key = "96f034fccf510760cbd63da0f70d4a9d";

        /// @brief A multicast transport that hands out queued datagrams from another router and records what is sent.
        class loopback final: public kn::datagram_transport
        {
        public:
            bool joined {};
            std::vector<sv::octets_t> sent {};
            std::deque<sv::octets_t> incoming {};

            [[nodiscard]] task_returning_expected_size_t send(const cspan_byte_t payload, const sockaddr*,
                                                              const ::socklen_t) noexcept(false) override
            {
                const auto* octets = reinterpret_cast<const std::uint8_t*>(payload.data());
                sent.emplace_back(octets, octets + payload.size());
                co_return expected_size_t {payload.size()};
            }

            [[nodiscard]] task_returning_expected_size_t receive(const span_byte_t buffer,
                                                                 kn::transport_peer& peer) noexcept(false) override
            {
                if (incoming.empty())
                    co_return std::unexpected(make_error_code(error::timeout));
                const auto packet = std::move(incoming.front());
                incoming.pop_front();
                peer = {};
                auto& sender = reinterpret_cast<sockaddr_in&>(peer.address);
                sender.sin_family = AF_INET;
                sender.sin_port = htons(3671u);
                sender.sin_addr.s_addr = htonl(0x7F000002u);
                peer.length = sizeof(sockaddr_in);
                std::ranges::transform(packet, buffer.begin(),
                                       [](const std::uint8_t octet) noexcept { return static_cast<std::byte>(octet); });
                co_return expected_size_t {packet.size()};
            }

            [[nodiscard]] expected_void_t join_multicast_group(const kn::multicast_group_configuration&) noexcept override
            {
                joined = true;
                return {};
            }

            [[nodiscard]] expected_void_t leave_multicast_group(const kn::multicast_group_configuration&) noexcept override
            {
                joined = false;
                return {};
            }
        };

        [[nodiscard]] kr::secure_configuration configuration(const ks::serial_number_t& serial = own_serial) noexcept(false)
        {
            kr::secure_configuration value {};
            value.backbone_key = sv::key(backbone_key);
            value.serial_number = serial;
            return value;
        }

        /// @brief Runs one client operation to completion on a fresh executor.
        template <typename Result, typename Operation>
        [[nodiscard]] Result run(Operation&& operation) noexcept(false)
        {
            completion::executor executor;
            std::optional<Result> result {};
            auto body = [&]() -> task<void>
            {
                result.emplace(co_await operation());
                executor.stop();
            };
            executor.spawn(body());
            executor.run();
            REQUIRE(result.has_value());
            return std::move(*result);
        }

        /// @brief A TIMER_NOTIFY authenticated under the backbone key.
        [[nodiscard]] sv::octets_t timer_notify(const std::uint64_t timer_value, const ks::serial_number_t& serial,
                                                const ks::message_tag_t& tag) noexcept(false)
        {
            const auto notify = ks::make_timer_notify(sv::key(backbone_key), timer_value, serial, tag);
            REQUIRE(notify.has_value());
            sv::octets_t packet(ks::timer_notify_size, 0u);
            REQUIRE(ks::encode_timer_notify_packet(packet, *notify).has_value());
            return packet;
        }

        /// @brief An unwrapped ROUTING_INDICATION carrying @p cemi.
        [[nodiscard]] sv::octets_t indication(const cspan_uint8_t cemi) noexcept(false)
        {
            sv::octets_t packet(kn::frame::communication_header_size + cemi.size(), 0u);
            REQUIRE(kr::encode_indication_packet(packet, kr::indication {cemi}).has_value());
            return packet;
        }

        /// @brief Wraps a datagram the way another router on the backbone does.
        [[nodiscard]] sv::octets_t wrapped(const cspan_uint8_t plain, const std::uint64_t timer_value, const std::uint16_t tag,
                                           const std::uint16_t session_id = 0u) noexcept(false)
        {
            sv::octets_t packet(ks::wrapper_overhead + plain.size(), 0u);
            const ks::message_tag_t message_tag {static_cast<std::uint8_t>(tag >> 8u), static_cast<std::uint8_t>(tag & 0xFFu)};
            const ks::wrapper_fields fields {session_id, ks::encode_sequence(timer_value), other_serial, message_tag};
            REQUIRE(ks::seal_wrapper(packet, sv::key(backbone_key), fields, plain).has_value());
            return packet;
        }

        /// @brief The switch-off counterpart of the sample telegram, so two deliveries can be told apart.
        [[nodiscard]] sv::octets_t switch_off() noexcept(false)
        {
            sv::octets_t cemi(sample_cemi.begin(), sample_cemi.end());
            cemi.back() = 0x80u;
            return cemi;
        }

        /// @brief Starts @p client, sends its synchronisation request, and queues the answer another router gives: the
        ///        request's serial number and tag, and a timer of one hour.
        void synchronise(kr::client& client, loopback& transport, sv::scripted_entropy& entropy) noexcept(false)
        {
            entropy.tag(0xABCDu);
            REQUIRE(client.start().has_value());
            REQUIRE(client.next_timer_deadline_ms() == 0u);
            REQUIRE(!client.timer_synchronised());
            REQUIRE(run<expected_void_t>([&] { return client.notify_timer(); }).has_value());
            REQUIRE(transport.sent.size() == 1u);
            transport.incoming.push_back(timer_notify(one_hour_ms, own_serial, {0xABu, 0xCDu}));
        }

        /// @brief Receives one event and returns the cEMI of the indication it must be.
        [[nodiscard]] sv::octets_t received_cemi(kr::client& client) noexcept(false)
        {
            const auto received = run<kr::event_result_t>([&] { return client.receive_event(); });
            REQUIRE(received.has_value());
            const auto* value = std::get_if<kr::received_indication>(&*received);
            REQUIRE(value != nullptr);
            const auto octets = value->cemi_bytes.span();
            return {octets.begin(), octets.end()};
        }
    }

    TEST_CASE("knx secure routing client refuses to start without a serial number or a key", "[knx][secure][routing][unit]")
    {
        detail::loopback transport {};
        sv::scripted_entropy entropy {};
        kr::client zero_serial {
            transport, {}, {.settings = detail::configuration(ks::serial_number_t {}), .clock_ms = detail::clock, .entropy = &entropy}};
        CHECK(zero_serial.secured());
        CHECK(zero_serial.start().error() == make_error_code(error::invalid_configuration));
        CHECK(!transport.joined);

        kr::secure_configuration keyless {};
        keyless.serial_number = detail::own_serial;
        kr::client without_key {transport, {}, {.settings = std::move(keyless), .clock_ms = detail::clock, .entropy = &entropy}};
        CHECK(without_key.start().error() == make_error_code(error::secure_key_missing));

        kr::client plain {transport};
        CHECK(!plain.secured());
        CHECK(plain.secure_counters().authentication_failures == 0u);
        REQUIRE(plain.start().has_value());
        CHECK(detail::run<expected_void_t>([&] { return plain.notify_timer(); }).error() == make_error_code(error::invalid_configuration));
    }

    TEST_CASE("knx secure routing client synchronises, delivers wrapped frames and sends only wrapped ones", "[knx][secure][routing][unit]")
    {
        detail::loopback transport {};
        sv::scripted_entropy entropy {};
        kr::client client {transport, {}, {.settings = detail::configuration(), .clock_ms = detail::clock, .entropy = &entropy}};
        detail::now_ms = 1'000u;
        detail::synchronise(client, transport, entropy);

        const auto request = ks::decode_timer_notify_packet(transport.sent.front());
        REQUIRE(request.has_value());
        CHECK(ks::verify_timer_notify(sv::key(detail::backbone_key), *request).has_value());
        CHECK(request->serial_number == detail::own_serial);
        CHECK(request->message_tag == ks::message_tag_t {0xABu, 0xCDu});
        CHECK(client.next_timer_deadline_ms() == 1'000u + 3'300u);

        transport.incoming.push_back(detail::wrapped(detail::indication(sample_cemi), detail::one_hour_ms + 10u, 0x0001u));
        CHECK(std::ranges::equal(detail::received_cemi(client), sample_cemi));
        CHECK(client.next_timer_deadline_ms() >= 1'000u + 10'000u);

        const kr::indication value {sample_cemi};
        REQUIRE(detail::run<expected_void_t>([&] { return client.send_indication(value); }).has_value());
        const auto sent = ks::decode_wrapper_packet(transport.sent.back());
        REQUIRE(sent.has_value());
        CHECK(sent->serial_number == detail::own_serial);
        CHECK(ks::decode_sequence(sent->sequence) == detail::one_hour_ms + 10u);
        std::array<std::uint8_t, kn::frame::max_datagram_size> plain {};
        const auto opened = ks::open_wrapper(plain, sv::key(detail::backbone_key), *sent);
        REQUIRE(opened.has_value());
        CHECK(std::ranges::equal(std::span {plain}.first(*opened), detail::indication(sample_cemi)));
        CHECK(client.secure_counters().timer_notifications_sent == 1u);
    }

    TEST_CASE("knx secure routing client drops forged, repeated and unencrypted frames", "[knx][secure][routing][unit]")
    {
        detail::loopback transport {};
        sv::scripted_entropy entropy {};
        kr::client client {transport, {}, {.settings = detail::configuration(), .clock_ms = detail::clock, .entropy = &entropy}};
        detail::now_ms = 1'000u;
        detail::synchronise(client, transport, entropy);

        const auto on = detail::indication(sample_cemi);
        auto forged = detail::wrapped(on, 2u * detail::one_hour_ms, 0x0002u); // a far-future timer...
        forged.back() ^= 0x01u;                                               // ...under a MAC that does not verify
        const auto first = detail::wrapped(on, detail::one_hour_ms, 0x0003u);
        transport.incoming.push_back(forged);
        transport.incoming.push_back(on);
        transport.incoming.push_back(first);
        transport.incoming.push_back(first);
        transport.incoming.push_back(detail::wrapped(detail::indication(detail::switch_off()), detail::one_hour_ms, 0x0004u));

        CHECK(std::ranges::equal(detail::received_cemi(client), sample_cemi));
        CHECK(detail::received_cemi(client) == detail::switch_off());
        CHECK(detail::run<kr::event_result_t>([&] { return client.receive_event(); }).error() == make_error_code(error::timeout));

        const auto& counters = client.secure_counters();
        CHECK(counters.authentication_failures == 1u);
        CHECK(counters.unencrypted_refused == 1u);
        CHECK(counters.duplicates == 1u);

        // P2 and P5: the forged far-future timer moved nothing and scheduled no update notify.
        CHECK(client.next_timer_deadline_ms() >= detail::now_ms + 10'000u);
        const kr::indication value {sample_cemi};
        REQUIRE(detail::run<expected_void_t>([&] { return client.send_indication(value); }).has_value());
        const auto sent = ks::decode_wrapper_packet(transport.sent.back());
        REQUIRE(sent.has_value());
        CHECK(ks::decode_sequence(sent->sequence) == detail::one_hour_ms);
    }

    TEST_CASE("knx secure routing client refuses wrappers that are not routing's", "[knx][secure][routing][unit]")
    {
        detail::loopback transport {};
        sv::scripted_entropy entropy {};
        kr::client client {transport, {}, {.settings = detail::configuration(), .clock_ms = detail::clock, .entropy = &entropy}};
        detail::now_ms = 1'000u;
        detail::synchronise(client, transport, entropy);

        const auto on = detail::indication(sample_cemi);
        const auto search = sv::hex("06 10 02 01 00 0e 08 01 7f 00 00 01 0e 57");
        transport.incoming.push_back(detail::wrapped(search, detail::one_hour_ms, 0x0010u));
        transport.incoming.push_back(detail::wrapped(on, detail::one_hour_ms, 0x0011u, 1u));
        transport.incoming.push_back(detail::wrapped(on, detail::one_hour_ms, 0x0012u));

        CHECK(std::ranges::equal(detail::received_cemi(client), sample_cemi));
        CHECK(client.secure_counters().refused_services == 1u);
        CHECK(client.secure_counters().authentication_failures == 1u);
    }

    TEST_CASE("knx secure routing client drops its own reflected wrappers", "[knx][secure][routing][unit]")
    {
        detail::loopback transport {};
        sv::scripted_entropy entropy {};
        kr::client client {transport, {}, {.settings = detail::configuration(), .clock_ms = detail::clock, .entropy = &entropy}};
        detail::now_ms = 1'000u;
        detail::synchronise(client, transport, entropy);

        const kr::indication value {sample_cemi};
        REQUIRE(detail::run<expected_void_t>([&] { return client.send_indication(value); }).has_value());
        transport.incoming.push_back(transport.sent.back());
        transport.incoming.push_back(detail::wrapped(detail::indication(detail::switch_off()), detail::one_hour_ms, 0x0020u));

        CHECK(detail::received_cemi(client) == detail::switch_off());
        CHECK(client.counters().reflected_messages == 1u);
    }

    TEST_CASE("knx secure routing client leaves unencrypted discovery to its caller", "[knx][secure][routing][unit]")
    {
        detail::loopback transport {};
        sv::scripted_entropy entropy {};
        kr::client client {transport, {}, {.settings = detail::configuration(), .clock_ms = detail::clock, .entropy = &entropy}};
        detail::now_ms = 1'000u;
        detail::synchronise(client, transport, entropy);

        transport.incoming.push_back(sv::hex("06 10 02 01 00 0e 08 01 7f 00 00 01 0e 57"));
        const auto received = detail::run<kr::event_result_t>([&] { return client.receive_event(); });
        CHECK(received.error() == make_error_code(error::unsupported_service));
        CHECK(client.secure_counters().unencrypted_refused == 0u);
    }

    TEST_CASE("knx secure routing client keeps the time when nobody answers", "[knx][secure][routing][unit]")
    {
        detail::loopback transport {};
        sv::scripted_entropy entropy {};
        kr::client client {transport, {}, {.settings = detail::configuration(), .clock_ms = detail::clock, .entropy = &entropy}};
        detail::now_ms = 0u;
        REQUIRE(client.start().has_value());
        REQUIRE(detail::run<expected_void_t>([&] { return client.notify_timer(); }).has_value());
        CHECK(transport.sent.size() == 1u);

        detail::now_ms = 3'299u;
        REQUIRE(detail::run<expected_void_t>([&] { return client.notify_timer(); }).has_value());
        CHECK(transport.sent.size() == 1u);
        CHECK(client.next_timer_deadline_ms() == 3'300u);
        CHECK(!client.timer_synchronised());

        detail::now_ms = 3'300u;
        REQUIRE(detail::run<expected_void_t>([&] { return client.notify_timer(); }).has_value());
        CHECK(transport.sent.size() == 1u);
        CHECK(client.next_timer_deadline_ms() == 13'300u);
        CHECK(client.timer_synchronised());

        detail::now_ms = 13'300u;
        REQUIRE(detail::run<expected_void_t>([&] { return client.notify_timer(); }).has_value());
        REQUIRE(transport.sent.size() == 2u);
        const auto periodic = ks::decode_timer_notify_packet(transport.sent.back());
        REQUIRE(periodic.has_value());
        CHECK(periodic->serial_number == detail::own_serial);
        CHECK(ks::decode_sequence(periodic->timer_value) == 13'300u);
        CHECK(ks::verify_timer_notify(sv::key(detail::backbone_key), *periodic).has_value());
    }

    TEST_CASE("knx secure routing client forgets synchronisation state when it stops and restarts", "[knx][secure][routing][unit]")
    {
        detail::loopback transport {};
        sv::scripted_entropy entropy {};
        kr::client client {transport, {}, {.settings = detail::configuration(), .clock_ms = detail::clock, .entropy = &entropy}};
        detail::now_ms = 1'000u;
        detail::synchronise(client, transport, entropy);

        const auto accepted = detail::wrapped(detail::indication(sample_cemi), detail::one_hour_ms + 10u, 0x0040u);
        transport.incoming.push_back(accepted);
        CHECK(std::ranges::equal(detail::received_cemi(client), sample_cemi));
        CHECK(client.timer_synchronised());

        REQUIRE(client.stop().has_value());
        CHECK(!transport.joined);
        CHECK(!client.timer_synchronised());
        CHECK(client.next_timer_deadline_ms() == std::numeric_limits<std::uint64_t>::max());

        transport.sent.clear();
        transport.incoming.push_back(accepted);
        REQUIRE(client.start().has_value());
        CHECK(transport.joined);
        CHECK(!client.timer_synchronised());
        CHECK(client.next_timer_deadline_ms() == 0u);
        CHECK(detail::run<kr::event_result_t>([&] { return client.receive_event(); }).error() == make_error_code(error::timeout));

        entropy.tag(0xBCDEu);
        REQUIRE(detail::run<expected_void_t>([&] { return client.notify_timer(); }).has_value());
        REQUIRE(transport.sent.size() == 1u);
        transport.incoming.push_back(detail::timer_notify(detail::one_hour_ms, detail::own_serial, {0xBCu, 0xDEu}));
        transport.incoming.push_back(detail::wrapped(detail::indication(detail::switch_off()), detail::one_hour_ms + 20u, 0x0041u));

        CHECK(detail::received_cemi(client) == detail::switch_off());
        CHECK(client.timer_synchronised());
    }
}
