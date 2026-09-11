/// @file kmx/aio/knx/client_stream_test.cpp
/// @brief The tunnelling client over a stream transport: TCP HPAIs, no acknowledgements, send-only periodic work.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/timer.hpp>
#include <kmx/aio/knx/client.hpp>
#include <kmx/aio/test/knx/telegram.hpp>
#include <kmx/aio/test/knx/transport.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <netinet/in.h>
#include <system_error>
#include <vector>

namespace kmx::aio::test::knx::client_stream_test
{
    using namespace kmx::aio::knx;

    namespace detail
    {
        /// @brief The time the client under test reads.
        std::uint32_t now_ms {};

        [[nodiscard]] std::uint32_t clock_now() noexcept
        {
            return now_ms;
        }

        /// @brief The channel the stand-in server allocates.
        constexpr std::uint8_t channel = 7u;

        /// @brief The individual address the stand-in server assigns.
        constexpr individual_address assigned {1u, 1u, 20u};

        /// @brief A request naming UDP endpoints, which a client on a stream must replace.
        const connect_request_frame udp_request {
            .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3671u}, 0x01u},
            .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3672u}, 0x01u},
        };

        /// @brief Returns the error a result carries, or no error.
        template <typename Value>
        [[nodiscard]] std::error_code error_of(const std::expected<Value, std::error_code>& result) noexcept
        {
            return result.has_value() ? std::error_code {} : result.error();
        }

        /// @brief Encodes a TUNNELLING_REQUEST on @ref channel carrying @ref sample_cemi.
        [[nodiscard]] std::vector<std::uint8_t> indication(const std::uint8_t sequence)
        {
            std::vector<std::uint8_t> packet(sample_tunnelling_packet_size, 0u);
            REQUIRE(frame::encode_tunnelling_request_packet(packet, channel, sequence, sample_cemi).has_value());
            return packet;
        }

        /// @brief Returns the service each recorded packet carries, in order.
        [[nodiscard]] std::vector<std::uint16_t> services(const std::vector<std::vector<std::uint8_t>>& packets)
        {
            std::vector<std::uint16_t> result {};
            for (const auto& packet: packets)
                if (const auto header = frame::decode_communication_header(packet); header.has_value())
                    result.push_back(header->service_type);
            return result;
        }

        /// @brief A stand-in TCP tunnelling server behind a stream transport.
        /// @details Answers a connect, a heartbeat and a disconnect the way a server on a TCP connection does, and
        ///          sends nothing back for a tunnelling request. A receive with no deadline waits on the executor for
        ///          a frame to be queued; one with a deadline gives up at once when none is.
        class stream_transport final: public recording_transport
        {
        public:
            /// @brief The loop a receive with no deadline waits on.
            completion::executor* wait_executor {};
            /// @brief Whether a CONNECT_REQUEST is answered.
            bool answer_connect = true;
            /// @brief Whether a CONNECTIONSTATE_REQUEST is answered.
            bool answer_heartbeat = true;
            /// @brief The error every receive reports, when set.
            std::error_code receive_error {};
            /// @brief Whether a receive is waiting for a frame at this moment.
            bool receiving {};
            /// @brief How many times the connection was opened.
            std::size_t opens {};
            /// @brief How many times an open connection was closed.
            std::size_t closes {};
            /// @brief How many receives were started.
            std::size_t receives {};

            void enqueue(std::vector<std::uint8_t> packet) { frames_.push_back(std::move(packet)); }

            [[nodiscard]] bool stream_oriented() const noexcept override { return true; }

            [[nodiscard]] task_returning_expected_void_t open() noexcept(false) override
            {
                ++opens;
                open_ = true;
                co_return expected_void_t {};
            }

            void close() noexcept override
            {
                closes += open_ ? 1u : 0u;
                open_ = false;
            }

            [[nodiscard]] task_returning_expected_size_t send(const cspan_byte_t payload, const sockaddr* const peer,
                                                              const ::socklen_t peer_length) noexcept(false) override
            {
                record_send(payload, peer, peer_length);
                answer({reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()});
                co_return expected_size_t {payload.size()};
            }

            [[nodiscard]] task_returning_expected_size_t receive(const span_byte_t buffer, transport_peer& peer) noexcept(false) override
            {
                ++receives;
                receiving = true;
                for (auto waits = 0u; frames_.empty() && !receive_error && (wait_executor != nullptr) && (waits < 1'000u); ++waits)
                {
                    completion::timer timer {*wait_executor};
                    static_cast<void>(co_await timer.wait(std::chrono::milliseconds {2}));
                }
                receiving = false;
                co_return take(buffer, peer);
            }

            [[nodiscard]] task_returning_expected_size_t receive_until(const span_byte_t buffer, transport_peer& peer,
                                                                       const std::uint32_t) noexcept(false) override
            {
                ++receives;
                co_return take(buffer, peer);
            }

        private:
            [[nodiscard]] static std::vector<std::uint8_t> connect_response()
            {
                std::vector<std::uint8_t> packet(20u, 0u);
                const connect_response_frame value {
                    .channel_id = channel,
                    .status = connect_status::no_error,
                    .data_endpoint = hpai {{}, 0x02u},
                    .assigned_address = assigned,
                };
                REQUIRE(connection::encode_connect_response_packet(packet, value).has_value());
                return packet;
            }

            [[nodiscard]] static std::vector<std::uint8_t> connectionstate_response()
            {
                std::vector<std::uint8_t> packet(8u, 0u);
                const connectionstate_response_frame value {channel, connect_status::no_error};
                REQUIRE(connection::encode_connectionstate_response_packet(packet, value).has_value());
                return packet;
            }

            [[nodiscard]] static std::vector<std::uint8_t> disconnect_response()
            {
                std::vector<std::uint8_t> packet(8u, 0u);
                REQUIRE(connection::encode_disconnect_response_packet(packet, disconnect_response_frame {channel, connect_status::no_error})
                            .has_value());
                return packet;
            }

            void answer(const cspan_uint8_t packet)
            {
                const auto header = frame::decode_communication_header(packet);
                if (!header.has_value())
                    return;
                if ((header->service_type == connection::connect_request_service) && answer_connect)
                    enqueue(connect_response());
                else if ((header->service_type == connection::connectionstate_request_service) && answer_heartbeat)
                    enqueue(connectionstate_response());
                else if (header->service_type == connection::disconnect_request_service)
                    enqueue(disconnect_response());
            }

            [[nodiscard]] expected_size_t take(const span_byte_t buffer, transport_peer& peer)
            {
                if (receive_error)
                    return std::unexpected(receive_error);
                if (frames_.empty())
                    return std::unexpected(make_error_code(error::timeout));
                const auto next = std::move(frames_.front());
                frames_.pop_front();
                fill_peer(peer, false, INADDR_LOOPBACK, 3671u);
                return deliver(next, buffer);
            }

            std::deque<std::vector<std::uint8_t>> frames_ {};
            bool open_ {};
        };

        /// @brief Returns the address the stand-in server answers from.
        [[nodiscard]] sockaddr_in server_address() noexcept
        {
            sockaddr_in address {};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons(3671u);
            return address;
        }

        /// @brief The transport, client and executor each test starts from.
        struct stream_client_fixture
        {
            stream_transport transport;
            sockaddr_in server = server_address();
            tunnelling_client client {transport, reinterpret_cast<const sockaddr*>(&server), sizeof(server),
                                      tunnelling_config {.connectionstate_timeout_ms = 1'000u, .heartbeat_failure_limit = 2u}, clock_now};
            completion::executor executor;

            stream_client_fixture() noexcept
            {
                now_ms = 0u;
                transport.wait_executor = &executor;
            }

            /// @brief Spawns @p work and runs the loop until the work stops it.
            void spawn_and_run(task<void> work) noexcept(false)
            {
                executor.spawn(std::move(work));
                executor.run();
            }
        };
    }

    TEST_CASE_METHOD(detail::stream_client_fixture, "knx tunnelling client over a stream opens the connection and asks with TCP HPAIs",
                     "[knx][client][tcp][unit]")
    {
        auto request = detail::udp_request;
        request.requested_address = detail::assigned;
        bool connected {};
        auto run = [&]() -> task<void>
        {
            connected = (co_await client.connect(request)).has_value();
            executor.stop();
        };
        spawn_and_run(run());

        CHECK(connected);
        CHECK(transport.opens == 1u);
        CHECK(client.channel_id() == detail::channel);
        CHECK(client.assigned_address() == detail::assigned);
        REQUIRE(transport.sent_packets().size() == 1u);
        CHECK(transport.sent_packets().front().size() == 28u);
        const auto sent = connection::decode_connect_request_packet(transport.sent_packets().front());
        REQUIRE(sent.has_value());
        for (const auto& endpoint: {sent->control_endpoint, sent->data_endpoint})
        {
            CHECK(endpoint.protocol == 0x02u);
            CHECK(endpoint.endpoint.port == 0u);
            CHECK(std::ranges::all_of(endpoint.endpoint.address, [](const std::uint8_t octet) noexcept { return octet == 0u; }));
        }
        CHECK(sent->requested_address == detail::assigned);
    }

    TEST_CASE_METHOD(detail::stream_client_fixture, "knx tunnelling client over a stream sends without waiting for an acknowledgement",
                     "[knx][client][tcp][unit]")
    {
        bool first {};
        bool second {};
        auto run = [&]() -> task<void>
        {
            if ((co_await client.connect(detail::udp_request)).has_value())
            {
                first = (co_await client.send(sample_cemi)).has_value();
                second = (co_await client.send(sample_cemi_read)).has_value();
            }
            executor.stop();
        };
        spawn_and_run(run());

        CHECK(first);
        CHECK(second);
        CHECK(client.connected());
        // The connect read its answer; neither request waited for one.
        CHECK(transport.receives == 1u);
        REQUIRE(transport.sent_packets().size() == 3u);
        for (std::uint8_t sequence = 0u; sequence < 2u; ++sequence)
        {
            const auto request = frame::decode_tunnelling_request_packet(transport.sent_packets()[1u + sequence]);
            REQUIRE(request.has_value());
            CHECK(request->channel_id == detail::channel);
            CHECK(request->sequence_number == sequence);
        }
    }

    TEST_CASE_METHOD(detail::stream_client_fixture, "knx tunnelling client over a stream neither acknowledges indications nor orders them",
                     "[knx][client][tcp][unit]")
    {
        std::size_t delivered {};
        auto run = [&]() -> task<void>
        {
            if (!(co_await client.connect(detail::udp_request)).has_value())
            {
                executor.stop();
                co_return;
            }
            for (const auto sequence: std::array<std::uint8_t, 3u> {5u, 2u, 2u})
            {
                transport.enqueue(detail::indication(sequence));
                if ((co_await client.receive_cemi()).has_value())
                    ++delivered;
            }
            executor.stop();
        };
        spawn_and_run(run());

        CHECK(delivered == 3u);
        const auto sent = detail::services(transport.sent_packets());
        CHECK(std::ranges::find(sent, frame::tunnelling_ack_service) == sent.end());
    }

    TEST_CASE_METHOD(detail::stream_client_fixture, "knx tunnelling client over a stream reports an unanswered heartbeat from poll",
                     "[knx][client][tcp][unit]")
    {
        transport.answer_heartbeat = false;
        bool sent {};
        bool quiet_before_deadline {};
        std::error_code first {};
        std::error_code second {};
        auto run = [&]() -> task<void>
        {
            if ((co_await client.connect(detail::udp_request)).has_value() && (co_await client.heartbeat()).has_value())
            {
                detail::now_ms = 999u;
                quiet_before_deadline = client.poll().has_value();
                detail::now_ms = 1'000u;
                first = detail::error_of(client.poll());
                sent = (co_await client.heartbeat()).has_value();
                detail::now_ms = 2'000u;
                second = detail::error_of(client.poll());
            }
            executor.stop();
        };
        spawn_and_run(run());

        CHECK(sent);
        CHECK(quiet_before_deadline);
        CHECK(first == make_error_code(error::connection_failed));
        CHECK(second == make_error_code(error::heartbeat_failed));
        CHECK(client.closed());
        CHECK(transport.closes == 1u);
        // Neither heartbeat waited for its answer.
        CHECK(transport.receives == 1u);
    }

    TEST_CASE_METHOD(detail::stream_client_fixture, "knx tunnelling client over a stream sends and beats beside a waiting receive",
                     "[knx][client][tcp][unit]")
    {
        bool overlapped {};
        bool sent {};
        bool beat {};
        bool receive_done {};
        expected_byte_buffer_t received = std::unexpected(make_error_code(error::timeout));
        auto receiver = [&]() -> task<void>
        {
            received = co_await client.receive_cemi();
            receive_done = true;
        };
        auto run = [&]() -> task<void>
        {
            if (!(co_await client.connect(detail::udp_request)).has_value())
            {
                executor.stop();
                co_return;
            }
            executor.spawn(receiver());
            for (auto waits = 0u; !transport.receiving && (waits < 500u); ++waits)
            {
                completion::timer timer {executor};
                static_cast<void>(co_await timer.wait(std::chrono::milliseconds {2}));
            }
            overlapped = transport.receiving;
            sent = (co_await client.send(sample_cemi)).has_value();
            beat = (co_await client.heartbeat()).has_value();
            transport.enqueue(detail::indication(0u));
            for (auto waits = 0u; !receive_done && (waits < 500u); ++waits)
            {
                completion::timer timer {executor};
                static_cast<void>(co_await timer.wait(std::chrono::milliseconds {2}));
            }
            executor.stop();
        };
        spawn_and_run(run());

        CHECK(overlapped);
        CHECK(sent);
        CHECK(beat);
        REQUIRE(receive_done);
        REQUIRE(received.has_value());
        CHECK(std::ranges::equal(*received, sample_cemi));
        // The heartbeat's answer reached the receive, so poll has nothing to count against the tunnel.
        detail::now_ms = 60'000u;
        CHECK(client.poll().has_value());
        const auto sent_services = detail::services(transport.sent_packets());
        CHECK(std::ranges::find(sent_services, frame::tunnelling_ack_service) == sent_services.end());
    }

    TEST_CASE_METHOD(detail::stream_client_fixture, "knx tunnelling client over a stream reconnects over a new connection",
                     "[knx][client][tcp][unit]")
    {
        bool disconnected {};
        std::size_t closes_after_disconnect {};
        bool reconnected {};
        auto run = [&]() -> task<void>
        {
            if ((co_await client.connect(detail::udp_request)).has_value())
                disconnected = (co_await client.disconnect()).has_value();
            closes_after_disconnect = transport.closes;
            client.reset();
            reconnected = (co_await client.connect(detail::udp_request)).has_value();
            executor.stop();
        };
        spawn_and_run(run());

        CHECK(disconnected);
        CHECK(closes_after_disconnect == 1u);
        CHECK(reconnected);
        CHECK(transport.opens == 2u);
        CHECK(client.connected());
    }

    TEST_CASE_METHOD(detail::stream_client_fixture, "knx tunnelling client over a stream ends the tunnel with its connection",
                     "[knx][client][tcp][unit]")
    {
        std::error_code failure {};
        auto run = [&]() -> task<void>
        {
            if ((co_await client.connect(detail::udp_request)).has_value())
            {
                transport.receive_error = make_error_code(error::shutdown);
                failure = detail::error_of(co_await client.receive_cemi());
            }
            executor.stop();
        };
        spawn_and_run(run());

        CHECK(failure == make_error_code(error::shutdown));
        CHECK(client.closed());
        CHECK(transport.closes == 1u);
    }

    TEST_CASE_METHOD(detail::stream_client_fixture, "knx tunnelling client over a stream makes one connect attempt",
                     "[knx][client][tcp][unit]")
    {
        transport.answer_connect = false;
        std::error_code failure {};
        auto run = [&]() -> task<void>
        {
            failure = detail::error_of(co_await client.connect(detail::udp_request));
            executor.stop();
        };
        spawn_and_run(run());

        CHECK(failure == make_error_code(error::timeout));
        CHECK(transport.sent_packets().size() == 1u);
        CHECK(transport.closes == 1u);
        CHECK(client.closed());
    }

    TEST_CASE_METHOD(detail::stream_client_fixture, "knx tunnelling client over a stream refuses an IPv6 connect",
                     "[knx][client][tcp][unit]")
    {
        std::error_code failure {};
        auto run = [&]() -> task<void>
        {
            failure = detail::error_of(co_await client.connect(ipv6_connect_request_frame {}));
            executor.stop();
        };
        spawn_and_run(run());

        CHECK(failure == make_error_code(error::unsupported_hpai));
        CHECK(transport.opens == 0u);
    }

    TEST_CASE_METHOD(detail::stream_client_fixture, "knx tunnelling client over a stream disconnects once when a heartbeat answer comes first",
                     "[knx][client][tcp][unit]")
    {
        bool beat {};
        bool disconnected {};
        auto run = [&]() -> task<void>
        {
            if ((co_await client.connect(detail::udp_request)).has_value())
            {
                // The heartbeat's answer is queued ahead of the disconnect's, so the disconnect reads it first.
                beat = (co_await client.heartbeat()).has_value();
                disconnected = (co_await client.disconnect()).has_value();
            }
            executor.stop();
        };
        spawn_and_run(run());

        CHECK(beat);
        CHECK(disconnected);
        const auto sent = detail::services(transport.sent_packets());
        CHECK(std::ranges::count(sent, connection::disconnect_request_service) == 1);
        // Both requests carry the control endpoint the connect named - the TCP HPAI - and so are sixteen octets long.
        for (const auto& packet: transport.sent_packets())
        {
            const auto heartbeat = connection::decode_connectionstate_request_packet(packet);
            const auto disconnect = connection::decode_disconnect_request_packet(packet);
            if (heartbeat.has_value())
                CHECK(heartbeat->control_endpoint.protocol == 0x02u);
            if (disconnect.has_value())
                CHECK(disconnect->control_endpoint.protocol == 0x02u);
        }
        CHECK(std::ranges::count(sent, connection::connectionstate_request_service) == 1);
    }
}
