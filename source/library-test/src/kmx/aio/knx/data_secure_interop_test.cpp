/// @file kmx/aio/knx/data_secure_interop_test.cpp
/// @brief KNX Data Secure group communication against external peers: over KNX IP Secure routing with xknx or Calimero,
///        and through a tunnel with xknx.
/// @details script/feature/knx/interop/run-data-secure-interop.sh starts the peer and passes its port in
/// KMX_KNX_INTEROP_PORT; without it each case is skipped. Both ends key themselves from the vendored keyring.knxkeys, whose
/// one group key secures 1/1/1, and whose interfaces list 1.1.1 and 1.1.12 as each other's senders on it. The in-tree side
/// is 1.1.12, and the peer is 1.1.1. One secured switch-on travels to 1/1/1 and one secured switch-off comes back.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/knx/tcp_server.hpp>
#include <kmx/aio/completion/knx/udp_transport.hpp>
#include <kmx/aio/completion/timer.hpp>
#include <kmx/aio/completion/udp/endpoint.hpp>
#include <kmx/aio/knx/cemi.hpp>
#include <kmx/aio/knx/data_secure.hpp>
#include <kmx/aio/knx/dpt.hpp>
#include <kmx/aio/knx/keyring.hpp>
#include <kmx/aio/knx/routing.hpp>
#include <kmx/aio/knx/server.hpp>
#include <kmx/aio/test/knx/secure_vectors.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <mutex>
#include <net/if.h>
#include <netinet/in.h>
#include <stop_token>
#include <string>
#include <thread>

namespace kmx::aio::test::knx::data_secure_interop_test
{
    namespace kn = kmx::aio::knx;
    namespace ds = kmx::aio::knx::data_secure;
    namespace kr = kmx::aio::knx::routing;
    namespace ks = kmx::aio::knx::secure;
    namespace sv = kmx::aio::test::knx::secure_vectors;

    namespace detail
    {
        /// @brief 1/1/1: the one group keyring.knxkeys holds a key for.
        constexpr std::uint16_t secured_group = 0x0901u;
        /// @brief The in-tree side's individual address, and xknx's.
        const kn::individual_address local_address {1u, 1u, 12u};
        const kn::individual_address peer_address {1u, 1u, 1u};
        /// @brief This router's serial number: `kmx` and a four.
        constexpr ks::serial_number_t serial {0x00u, 0x00u, 0x6Bu, 0x6Du, 0x78u, 0x04u};

        [[nodiscard]] std::string environment(const char* const name, const std::string& fallback) noexcept(false)
        {
            const auto* const value = std::getenv(name);
            return (value == nullptr) ? fallback : std::string {value};
        }

        [[nodiscard]] std::chrono::seconds time_limit() noexcept(false)
        {
            return std::chrono::seconds {std::stoul(environment("KMX_KNX_INTEROP_TIMEOUT", "40"))};
        }

        [[nodiscard]] kn::keyring::document load_keyring() noexcept(false)
        {
            const auto path =
                environment("KMX_KNX_INTEROP_KEYRING", (sv::conformance_directory() / "keyrings" / "keyring.knxkeys").string());
            std::ifstream input(path, std::ios::binary);
            const std::string xml {std::istreambuf_iterator<char> {input}, std::istreambuf_iterator<char> {}};
            auto keyring = kn::keyring::load(xml, environment("KMX_KNX_INTEROP_PASSWORD", "pwd"));
            REQUIRE(keyring.has_value());
            return std::move(*keyring);
        }

        /// @brief The in-tree side's Data Secure configuration, from the keyring: the key of 1/1/1, and xknx as its sender.
        [[nodiscard]] ds::configuration data_secure_configuration(const kn::keyring::document& keyring) noexcept(false)
        {
            auto configuration = ds::configuration_for(keyring, local_address);
            REQUIRE(configuration.has_value());
            return std::move(*configuration);
        }

        /// @brief A switch telegram to 1/1/1.
        [[nodiscard]] byte_buffer_t switch_telegram(const kn::cemi_message_code code, const kn::individual_address source, const bool on)
        {
            const auto value = kn::dpt::encode<1u>(on);
            REQUIRE(value.has_value());
            std::array<std::uint8_t, kn::cemi::max_l_data_size> message {};
            const auto size = kn::cemi::encode(message, code, source, kn::group_address {secured_group}, kn::apci::group_value_write,
                                               value->apdu(), kn::l_data_options {});
            REQUIRE(size.has_value());
            return byte_buffer_t(message.begin(), message.begin() + static_cast<std::ptrdiff_t>(*size));
        }

        /// @brief Indicates whether a plain frame is a switch telegram to 1/1/1 from @p source with value @p on.
        [[nodiscard]] bool switch_from(const kn::cemi_frame& frame, const kn::individual_address source, const bool on) noexcept
        {
            return frame.group_addressed() && (frame.destination == secured_group) && (frame.source == source) &&
                   (frame.application_service == kn::apci::group_value_write) && (frame.compact_value == (on ? 1u : 0u));
        }

        /// @brief Runs the executor, stopping it when @p limit passes first.
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

        /// @brief What the routing exchange observed.
        struct routing_outcome
        {
            bool answered {};
            std::size_t sent {};
            std::size_t received {};
        };

        /// @brief Sends every TIMER_NOTIFY that falls due until xknx has answered.
        task<void> notify(kr::client& router, const routing_outcome& state, completion::executor& executor) noexcept(false)
        {
            completion::timer pause {executor};
            while (!state.answered)
            {
                static_cast<void>(co_await router.notify_timer());
                static_cast<void>(co_await pause.wait(std::chrono::milliseconds {50}));
            }
        }

        /// @brief Sends the switch-on twice a second, once the routing timer has synchronised, until xknx answers.
        task<void> send(kr::client& router, routing_outcome& state, completion::executor& executor) noexcept(false)
        {
            completion::timer pause {executor};
            const auto on = switch_telegram(kn::cemi_message_code::l_data_ind, local_address, true);
            while (!state.answered)
            {
                if (router.timer_synchronised())
                    state.sent += (co_await router.send_indication(kr::indication {.cemi_bytes = on})).has_value() ? 1u : 0u;
                static_cast<void>(co_await pause.wait(std::chrono::milliseconds {500}));
            }
        }

        /// @brief Receives until xknx's secured switch-off arrives, opened, then stops the executor.
        task<void> receive(kr::client& router, routing_outcome& state, completion::executor& executor) noexcept(false)
        {
            while (!state.answered)
            {
                const auto received = co_await router.receive_indication();
                state.received += received.has_value() ? 1u : 0u;
                state.answered = received.has_value() && switch_from(received->cemi, peer_address, false);
            }
            executor.stop();
        }

        /// @brief What the in-tree device saw of xknx's tunnel.
        struct device_outcome
        {
            bool opened {};
            bool confirmed {};
            bool answered {};
            bool released {};
        };

        /// @brief The device behind the in-tree server: opens xknx's switch-on, confirms it, and answers it secured.
        [[nodiscard]] kn::server_event_handler device(kn::generic_server& server, ds::context& context, device_outcome& observed)
        {
            return [&server, &context, &observed](kn::server_event event) -> task<void>
            {
                const auto frame = kn::cemi::decode(event.cemi_bytes);
                if (!frame.has_value() || !frame->group_addressed() || (frame->destination != secured_group))
                    co_return;
                const auto opened = context.open_frame(event.cemi_bytes);
                const auto plain = opened.has_value() ? kn::cemi::decode(*opened) : std::unexpected(kn::error::malformed_frame);
                observed.opened = plain.has_value() && switch_from(*plain, peer_address, true);
                // xknx sends nothing further until its request is confirmed; the confirmation carries the frame as it came.
                auto confirmation = event.cemi_bytes;
                confirmation[0u] = static_cast<std::uint8_t>(kn::cemi_message_code::l_data_con);
                observed.confirmed = (co_await server.send(event.channel_id, confirmation)).has_value();
                const auto answer = context.secure_frame(switch_telegram(kn::cemi_message_code::l_data_ind, local_address, false));
                if (answer.has_value())
                    observed.answered = (co_await server.send(event.channel_id, *answer)).has_value();
            };
        }

        task<void> run_server(completion::knx::tcp_server& tcp, std::atomic_bool& ended)
        {
            static_cast<void>(co_await tcp.serve());
            ended = true;
        }

        /// @brief Waits for the exchange to finish and xknx to close its tunnel, then stops the server and the executor.
        task<void> await_peer(completion::executor& executor, kn::generic_server& server, completion::knx::tcp_server& tcp,
                              device_outcome& observed, const std::atomic_bool& serving_ended)
        {
            completion::timer pause {executor};
            while (!observed.answered || (server.active_channels() != 0u) || (tcp.connections() != 0u))
                static_cast<void>(co_await pause.wait(std::chrono::milliseconds {20}));
            observed.released = true;
            tcp.stop();
            while (!serving_ended)
                static_cast<void>(co_await pause.wait(std::chrono::milliseconds {5}));
            executor.stop();
        }
    }

    TEST_CASE("knx data secure router exchanges secured group telegrams with an external peer over secure routing",
              "[knx][data_secure][routing][interop]")
    {
        const auto port = detail::environment("KMX_KNX_INTEROP_PORT", "");
        if (port.empty())
            SKIP("KMX_KNX_INTEROP_PORT is unset: script/feature/knx/interop/run-data-secure-interop.sh starts xknx and sets it");

        const auto keyring = detail::load_keyring();
        auto settings = kn::keyring::routing_configuration_for(keyring, detail::serial);
        REQUIRE(settings.has_value());
        ds::context context {detail::data_secure_configuration(keyring)};
        const kr::multicast_configuration group {.group = settings->multicast_address,
                                                 .port = static_cast<std::uint16_t>(std::stoul(port)),
                                                 .interface_index =
                                                     ::if_nametoindex(detail::environment("KMX_KNX_INTEROP_INTERFACE", "lo").c_str()),
                                                 .loopback = true};
        completion::executor executor;
        auto endpoint = completion::udp::endpoint::create(executor, AF_INET);
        REQUIRE(endpoint.has_value());
        completion::knx::udp_transport transport {*endpoint};
        kr::client router {transport, group, std::move(*settings)};
        router.use_data_secure(&context);
        REQUIRE(router.start().has_value());

        detail::routing_outcome state {};
        executor.spawn(detail::notify(router, state, executor));
        executor.spawn(detail::send(router, state, executor));
        executor.spawn(detail::receive(router, state, executor));
        detail::run_bounded(executor, detail::time_limit());

        const auto counters = context.counters();
        INFO("sent=" << state.sent << " received=" << state.received << " authentication_failures=" << counters.authentication_failures
                     << " replays=" << counters.replays << " missing_keys=" << counters.missing_keys
                     << " unencrypted_refused=" << counters.unencrypted_refused << " refused_services=" << counters.refused_services);
        CHECK(state.answered);
        CHECK(counters.authentication_failures == 0u);
        CHECK(counters.missing_keys == 0u);
        CHECK(counters.unencrypted_refused == 0u);
    }

    TEST_CASE("knx data secure device answers xknx through the in-tree tunnelling server", "[knx][data_secure][tcp][interop]")
    {
        const auto port = detail::environment("KMX_KNX_INTEROP_PORT", "");
        if (port.empty())
            SKIP("KMX_KNX_INTEROP_PORT is unset: script/feature/knx/interop/run-data-secure-interop.sh starts xknx and sets it");

        const auto keyring = detail::load_keyring();
        ds::context context {detail::data_secure_configuration(keyring)};
        completion::executor executor;
        // The server passes Data Secure through untouched, and gives xknx's tunnel the address the keyring trusts.
        kn::generic_server server {kn::server_config {.first_assigned_address = detail::peer_address}};
        detail::device_outcome observed {};
        completion::knx::tcp_server tcp {executor,
                                         server,
                                         {.bind_address = {127u, 0u, 0u, 1u},
                                          .port = static_cast<port_t>(std::stoul(port)),
                                          .on_event = detail::device(server, context, observed)}};
        REQUIRE(tcp.listen().has_value());
        std::atomic_bool serving_ended {};
        executor.spawn(detail::run_server(tcp, serving_ended));
        executor.spawn(detail::await_peer(executor, server, tcp, observed, serving_ended));
        detail::run_bounded(executor, detail::time_limit());

        const auto counters = context.counters();
        INFO("authentication_failures=" << counters.authentication_failures << " replays=" << counters.replays << " missing_keys="
                                        << counters.missing_keys << " unencrypted_refused=" << counters.unencrypted_refused);
        CHECK(observed.opened);
        CHECK(observed.confirmed);
        CHECK(observed.answered);
        CHECK(observed.released);
        CHECK(counters.authentication_failures == 0u);
    }
}
