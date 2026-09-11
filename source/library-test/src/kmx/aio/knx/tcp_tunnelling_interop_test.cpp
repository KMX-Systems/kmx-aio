/// @file kmx/aio/knx/tcp_tunnelling_interop_test.cpp
/// @brief KNXnet/IP tunnelling over TCP against external peers: calimero-server for the in-tree client, xknx for the
///        in-tree server.
/// @details script/feature/knx/interop/run-tcp-tunnelling-interop.sh starts the peer and passes its port in
/// KMX_KNX_INTEROP_PORT; without it each case is skipped. Both exchanges are one switch-on each way: a request to 1/2/3
/// that the server confirms, and - toward an external client - an indication to 1/2/4 in answer.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/knx/tcp_server.hpp>
#include <kmx/aio/completion/knx/tcp_transport.hpp>
#include <kmx/aio/completion/timer.hpp>
#include <kmx/aio/knx/client.hpp>
#include <kmx/aio/knx/server.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <netinet/in.h>
#include <stop_token>
#include <string>
#include <thread>

namespace kmx::aio::test::knx::tcp_tunnelling_interop_test
{
    using namespace kmx::aio::knx;

    namespace detail
    {
        /// @brief The group the switch-on request goes to: 1/2/3.
        constexpr std::uint16_t request_group = 0x0A03u;
        /// @brief The group the in-tree server answers on: 1/2/4.
        constexpr std::uint16_t answer_group = 0x0A04u;

        /// @brief What the in-tree client saw of its tunnel to the external server.
        struct client_outcome
        {
            bool connected {};
            std::uint8_t channel {};
            std::uint16_t assigned {};
            bool sent {};
            std::size_t frames {};
            bool confirmed {};
            bool beat {};
            bool disconnected {};
        };

        /// @brief What the in-tree server saw of the external client's tunnel.
        struct server_outcome
        {
            bool requested {};
            bool confirmed {};
            bool answered {};
            bool released {};
        };

        [[nodiscard]] std::string environment(const char* const name, const std::string& fallback) noexcept(false)
        {
            const auto* const value = std::getenv(name);
            return (value == nullptr) ? fallback : std::string {value};
        }

        [[nodiscard]] std::chrono::seconds time_limit() noexcept(false)
        {
            return std::chrono::seconds {std::stoul(environment("KMX_KNX_INTEROP_TIMEOUT", "40"))};
        }

        [[nodiscard]] sockaddr_in loopback(const port_t port) noexcept
        {
            sockaddr_in address {};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons(port);
            return address;
        }

        /// @brief Runs the executor, stopping it after @p limit if the exchange has not stopped it first.
        void run_bounded(completion::executor& executor, const std::chrono::seconds limit) noexcept(false)
        {
            std::jthread watchdog(
                [&executor, limit](const std::stop_token& stop)
                {
                    std::mutex mutex;
                    std::condition_variable_any wake;
                    std::unique_lock lock(mutex);
                    static_cast<void>(wake.wait_for(lock, stop, limit, [] { return false; }));
                    if (!stop.stop_requested())
                        executor.stop();
                });
            executor.run();
        }

        /// @brief Indicates whether cEMI octets are an L_Data.req to group @p group.
        [[nodiscard]] bool request_to(const cspan_uint8_t octets, const std::uint16_t group) noexcept
        {
            if ((octets.size() < 2u) || (octets[0u] != static_cast<std::uint8_t>(cemi_message_code::l_data_req)))
                return false;
            const std::size_t control = 2u + octets[1u];
            if (octets.size() < (control + 6u))
                return false;
            const auto destination = static_cast<std::uint16_t>((octets[control + 4u] << 8u) | octets[control + 5u]);
            return ((octets[control + 1u] & 0x80u) != 0u) && (destination == group);
        }

        /// @brief Encodes the switch-on indication to 1/2/4 the in-tree server answers with.
        [[nodiscard]] byte_buffer_t answer_indication()
        {
            const auto value = dpt::encode<1u>(true);
            REQUIRE(value.has_value());
            std::array<std::uint8_t, cemi::max_l_data_size> message {};
            const auto size = cemi::encode(message, cemi_message_code::l_data_ind, individual_address {1u, 1u, 1u},
                                           group_address {answer_group}, apci::group_value_write, value->apdu(), l_data_options {});
            REQUIRE(size.has_value());
            return byte_buffer_t(message.begin(), message.begin() + static_cast<std::ptrdiff_t>(*size));
        }

        /// @brief Opens a tunnel, sends a switch-on and waits for its confirmation, sends a heartbeat, and disconnects.
        task<void> tunnel_to_peer(completion::executor& executor, tunnelling_client& client, const dpt::payload& switch_on,
                                  client_outcome& outcome)
        {
            outcome.connected = (co_await client.connect(connect_request_frame {})).has_value();
            if (outcome.connected)
            {
                outcome.channel = client.channel_id();
                outcome.assigned = client.assigned_address().value();
                outcome.sent = (co_await client.write_group_value(group_address {request_group}, switch_on)).has_value();
                // A KNXnet/IP server confirms each request it passes on to the bus, back through the tunnel.
                for (std::size_t attempt {}; outcome.sent && !outcome.confirmed && (attempt < 8u); ++attempt)
                {
                    const auto telegram = co_await client.receive_telegram();
                    if (!telegram.has_value())
                        break;
                    ++outcome.frames;
                    outcome.confirmed =
                        (telegram->frame.message_code == cemi_message_code::l_data_con) && (telegram->frame.destination == request_group);
                }
                outcome.beat = (co_await client.heartbeat()).has_value();
                outcome.disconnected = (co_await client.disconnect()).has_value();
            }
            executor.stop();
        }

        /// @brief Confirms the external client's switch-on request, as a server passing it to a bus would, then answers it.
        [[nodiscard]] server_event_handler answer_peer(generic_server& server, const byte_buffer_t& answer, server_outcome& outcome)
        {
            return [&server, &answer, &outcome](server_event event) -> task<void>
            {
                if (!request_to(event.cemi_bytes, request_group))
                    co_return;
                outcome.requested = true;
                // A client such as xknx sends nothing further until its request is confirmed.
                auto confirmation = event.cemi_bytes;
                confirmation[0u] = static_cast<std::uint8_t>(cemi_message_code::l_data_con);
                outcome.confirmed = (co_await server.send(event.channel_id, confirmation)).has_value();
                outcome.answered = (co_await server.send(event.channel_id, answer)).has_value();
            };
        }

        /// @brief Runs the accept loop, then records that it ended.
        task<void> run_server(completion::knx::tcp_server& tcp, std::atomic_bool& ended)
        {
            static_cast<void>(co_await tcp.serve());
            ended = true;
        }

        /// @brief Waits for the exchange to finish and the peer to close its tunnel, then stops the server and the executor.
        task<void> await_peer(completion::executor& executor, generic_server& server, completion::knx::tcp_server& tcp,
                              server_outcome& outcome, const std::atomic_bool& serving_ended)
        {
            completion::timer pause {executor};
            while (!outcome.answered || (server.active_channels() != 0u) || (tcp.connections() != 0u))
                static_cast<void>(co_await pause.wait(std::chrono::milliseconds {20}));
            outcome.released = true;
            tcp.stop();
            while (!serving_ended)
                static_cast<void>(co_await pause.wait(std::chrono::milliseconds {5}));
            executor.stop();
        }
    }

    TEST_CASE("knx tunnelling client tunnels over TCP to an external KNXnet/IP server", "[knx][tcp][client][interop]")
    {
        const auto port = detail::environment("KMX_KNX_INTEROP_PORT", "");
        if (port.empty())
            SKIP("KMX_KNX_INTEROP_PORT is unset: script/feature/knx/interop/run-tcp-tunnelling-interop.sh starts a peer and sets it");

        const auto switch_on = dpt::encode<1u>(true);
        REQUIRE(switch_on.has_value());
        completion::executor executor;
        const auto address = detail::loopback(static_cast<port_t>(std::stoul(port)));
        completion::knx::tcp_transport transport {executor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)};
        tunnelling_client client {transport, reinterpret_cast<const sockaddr*>(&address), sizeof(address)};
        detail::client_outcome outcome {};
        executor.spawn(detail::tunnel_to_peer(executor, client, *switch_on, outcome));
        detail::run_bounded(executor, detail::time_limit());

        INFO("channel=" << static_cast<int>(outcome.channel) << " assigned=0x" << std::hex << outcome.assigned << std::dec
                        << " frames=" << outcome.frames);
        CHECK(outcome.connected);
        CHECK(outcome.sent);
        CHECK(outcome.confirmed);
        CHECK(outcome.beat);
        CHECK(outcome.disconnected);
    }

    TEST_CASE("knx tunnelling server serves an external KNXnet/IP TCP tunnelling client", "[knx][tcp][server][interop]")
    {
        const auto port = detail::environment("KMX_KNX_INTEROP_PORT", "");
        if (port.empty())
            SKIP("KMX_KNX_INTEROP_PORT is unset: script/feature/knx/interop/run-tcp-tunnelling-interop.sh starts a peer and sets it");

        const auto answer = detail::answer_indication();
        completion::executor executor;
        generic_server server {server_config {.first_assigned_address = individual_address {1u, 1u, 240u}}};
        detail::server_outcome outcome {};
        completion::knx::tcp_server tcp {executor,
                                         server,
                                         {.bind_address = {127u, 0u, 0u, 1u},
                                          .port = static_cast<port_t>(std::stoul(port)),
                                          .on_event = detail::answer_peer(server, answer, outcome)}};
        REQUIRE(tcp.listen().has_value());
        std::atomic_bool serving_ended {};
        executor.spawn(detail::run_server(tcp, serving_ended));
        executor.spawn(detail::await_peer(executor, server, tcp, outcome, serving_ended));
        detail::run_bounded(executor, detail::time_limit());

        INFO("channels=" << static_cast<int>(server.active_channels()) << " connections=" << tcp.connections());
        CHECK(outcome.requested);
        CHECK(outcome.confirmed);
        CHECK(outcome.answered);
        CHECK(outcome.released);
    }
}
