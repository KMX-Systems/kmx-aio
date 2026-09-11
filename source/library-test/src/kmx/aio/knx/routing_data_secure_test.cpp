/// @file kmx/aio/knx/routing_data_secure_test.cpp
/// @brief KNX Data Secure applied by a routing client: what it puts on the group, and what it takes off it.
/// @details A loopback transport stands in for the multicast group. The sender is keyed as xknx's first Data Secure vector
/// was, so the indication it sends has to carry exactly the frame xknx secured.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/knx/data_secure.hpp>
#include <kmx/aio/knx/error.hpp>
#include <kmx/aio/knx/routing.hpp>
#include <kmx/aio/test/knx/secure_vectors.hpp>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <netinet/in.h>
#include <vector>

namespace kmx::aio::test::knx::routing_data_secure_test
{
    namespace kn = kmx::aio::knx;
    namespace ds = kmx::aio::knx::data_secure;
    namespace kr = kmx::aio::knx::keyring;
    namespace sv = kmx::aio::test::knx::secure_vectors;
    using kn::error;
    using kn::make_error_code;

    namespace detail
    {
        /// @brief xknx's first Data Secure vector: 1.1.5 switching 1/2/3 on, and the same telegram secured under sequence 1.
        constexpr std::string_view plain_cemi = "2900bce011050a03010081";
        constexpr std::string_view secured_cemi = "2900bce011050a030e03f1100000000000011618845d3621";
        constexpr std::string_view group_key = "d2effbdc88eb5c9a57a73b0aad64996f";

        /// @brief The multicast group, as a queue of datagrams and a record of the last one sent.
        class loopback_group final: public kn::datagram_transport
        {
        public:
            std::vector<std::uint8_t> last_sent {};

            void enqueue(std::vector<std::uint8_t> packet) { incoming_.push_back(std::move(packet)); }

            [[nodiscard]] task_returning_expected_size_t send(const cspan_byte_t payload, const sockaddr*,
                                                              const ::socklen_t) noexcept(false) override
            {
                const auto* const octets = reinterpret_cast<const std::uint8_t*>(payload.data());
                last_sent.assign(octets, octets + payload.size());
                co_return payload.size();
            }

            [[nodiscard]] task_returning_expected_size_t receive(const span_byte_t buffer,
                                                                 kn::transport_peer& peer) noexcept(false) override
            {
                if (incoming_.empty())
                    co_return std::unexpected(make_error_code(error::timeout));
                const auto packet = std::move(incoming_.front());
                incoming_.pop_front();
                peer = {};
                auto& sender = reinterpret_cast<sockaddr_in&>(peer.address);
                sender.sin_family = AF_INET;
                sender.sin_port = htons(3671u);
                sender.sin_addr.s_addr = htonl(0x7F000002u);
                peer.length = sizeof(sockaddr_in);
                std::ranges::transform(packet, buffer.begin(), [](const std::uint8_t octet) { return static_cast<std::byte>(octet); });
                co_return packet.size();
            }

            [[nodiscard]] expected_void_t join_multicast_group(const kn::multicast_group_configuration&) noexcept override { return {}; }
            [[nodiscard]] expected_void_t leave_multicast_group(const kn::multicast_group_configuration&) noexcept override { return {}; }

        private:
            std::deque<std::vector<std::uint8_t>> incoming_ {};
        };

        /// @brief Starts its sequence numbers where it is told to, and records nothing.
        class fixed_store final: public ds::sequence_store
        {
        public:
            explicit fixed_store(const std::uint64_t first) noexcept: first_(first) {}

            [[nodiscard]] std::expected<std::uint64_t, std::error_code> load() noexcept override { return first_; }
            [[nodiscard]] expected_void_t reserve_until(const std::uint64_t limit) noexcept override
            {
                first_ = limit;
                return {};
            }

        private:
            std::uint64_t first_ {};
        };

        [[nodiscard]] ds::configuration configuration()
        {
            ds::configuration value {};
            value.group_keys.push_back(kr::group_key {.address = kn::group_address {0x0A03u}, .key = sv::key(sv::hex(group_key))});
            value.senders.push_back(ds::sender_sequence {.address = kn::individual_address {1u, 1u, 5u}, .last_valid_sequence = 0u});
            return value;
        }

        /// @brief A ROUTING_INDICATION carrying @p cemi.
        [[nodiscard]] std::vector<std::uint8_t> routing_packet(const cspan_uint8_t cemi)
        {
            const auto length = static_cast<std::uint16_t>(6u + cemi.size());
            std::vector<std::uint8_t> packet {
                0x06u, 0x10u, 0x05u, 0x30u, static_cast<std::uint8_t>(length >> 8u), static_cast<std::uint8_t>(length & 0xFFu)};
            packet.insert(packet.end(), cemi.begin(), cemi.end());
            return packet;
        }
    }

    TEST_CASE("knx routing client secures the group telegrams it sends and opens those it receives", "[knx][data_secure][routing][unit]")
    {
        const auto plain = sv::hex(detail::plain_cemi);
        detail::loopback_group sending_group {};
        kn::routing::client sender {sending_group};
        REQUIRE(sender.start().has_value());
        detail::fixed_store store {1u};
        ds::context sending_context {detail::configuration(), &store};
        sender.use_data_secure(&sending_context);

        detail::loopback_group receiving_group {};
        kn::routing::client receiver {receiving_group};
        REQUIRE(receiver.start().has_value());
        ds::context receiving_context {detail::configuration()};
        receiver.use_data_secure(&receiving_context);

        completion::executor executor;
        bool sent {};
        std::vector<std::uint8_t> received {};
        auto run = [&]() -> task<void>
        {
            sent = (co_await sender.send_indication(kn::routing::indication {.cemi_bytes = plain})).has_value();
            // An unsecured telegram to the secured group comes first; it is refused and read past.
            receiving_group.enqueue(detail::routing_packet(plain));
            receiving_group.enqueue(sending_group.last_sent);
            const auto indication = co_await receiver.receive_indication();
            if (indication.has_value())
                received.assign(indication->cemi_bytes.begin(), indication->cemi_bytes.end());
            executor.stop();
        };
        executor.spawn(run());
        executor.run();

        CHECK(sent);
        CHECK(sending_group.last_sent == detail::routing_packet(sv::hex(detail::secured_cemi)));
        CHECK(received == plain);
        CHECK(receiving_context.counters().unencrypted_refused == 1u);
    }
}
