/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/knx/udp_transport.hpp>
#include <kmx/aio/completion/timer.hpp>
#include <kmx/aio/completion/udp/endpoint.hpp>
#include <kmx/aio/knx/keyring.hpp>
#include <kmx/aio/knx/routing.hpp>
#include <kmx/aio/test/knx/secure_vectors.hpp>
#include <kmx/aio/test/knx/telegram.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <mutex>
#include <net/if.h>
#include <stop_token>
#include <string>
#include <thread>

namespace kmx::aio::test::knx::secure::routing_secure_interop_test
{
    namespace kn = kmx::aio::knx;
    namespace kr = kmx::aio::knx::routing;
    namespace ks = kmx::aio::knx::secure;
    namespace sv = kmx::aio::test::knx::secure_vectors;

    namespace detail
    {
        /// @brief The group the peer answers on: 1/2/4.
        inline constexpr std::uint16_t answer_group = 0x0A04u;
        /// @brief This router's serial number: `kmx` and a one.
        inline constexpr ks::serial_number_t serial {0x00u, 0x00u, 0x6Bu, 0x6Du, 0x78u, 0x01u};

        [[nodiscard]] std::string environment(const char* const name, const std::string& fallback) noexcept(false)
        {
            const auto* const value = std::getenv(name);
            return (value == nullptr) ? fallback : std::string {value};
        }

        /// @brief Indicates whether a cEMI L_Data frame is addressed to group @p group.
        [[nodiscard]] bool addressed_to(const kr::received_indication& value, const std::uint16_t group) noexcept
        {
            const auto octets = value.cemi_bytes.span();
            if (octets.size() < 2u)
                return false;
            const std::size_t control = 2u + octets[1u];
            if (octets.size() < (control + 6u))
                return false;
            const auto destination = static_cast<std::uint16_t>((octets[control + 4u] << 8u) | octets[control + 5u]);
            return ((octets[control + 1u] & 0x80u) != 0u) && (destination == group);
        }

        /// @brief What the exchange with the peer observed.
        struct outcome
        {
            bool answered {};
            std::size_t sent {};
            std::size_t received {};
        };

        /// @brief Sends every TIMER_NOTIFY that falls due until the peer has answered.
        task<void> notify(kr::client& client, const outcome& state, completion::executor& executor) noexcept(false)
        {
            completion::timer pause {executor};
            while (!state.answered)
            {
                (void) co_await client.notify_timer();
                (void) co_await pause.wait(std::chrono::milliseconds {50});
            }
        }

        /// @brief Sends the switch-on to 1/2/3 twice a second, once the timer has synchronised, until the peer answers.
        /// @details The wait matters: the peer answers the moment it hears the switch-on, and an answer that reaches a
        ///          router still synchronising is dropped like any other early wrapper.
        task<void> send(kr::client& client, outcome& state, completion::executor& executor) noexcept(false)
        {
            completion::timer pause {executor};
            // Routing carries L_Data.ind; a routing peer drops the L_Data.req the tunnelling sample telegram is.
            auto cemi = sample_cemi;
            cemi[0u] = 0x29u;
            const kr::indication on {cemi};
            while (!state.answered)
            {
                if (client.timer_synchronised())
                    state.sent += (co_await client.send_indication(on)).has_value() ? 1u : 0u;
                (void) co_await pause.wait(std::chrono::milliseconds {500});
            }
        }

        /// @brief Receives until the peer's answer to 1/2/4 arrives, then stops the executor.
        task<void> receive(kr::client& client, outcome& state, completion::executor& executor) noexcept(false)
        {
            while (!state.answered)
            {
                const auto received = co_await client.receive_indication();
                state.received += received.has_value() ? 1u : 0u;
                state.answered = received.has_value() && addressed_to(*received, answer_group);
            }
            executor.stop();
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
                    (void) wake.wait_for(lock, stop, limit, [] { return false; });
                    if (!stop.stop_requested())
                        executor.stop();
                });
            executor.run();
        }
    } // namespace detail

    TEST_CASE("knx secure router exchanges telegrams with an external KNX IP Secure routing peer", "[knx][secure][routing][interop]")
    {
        const auto port = detail::environment("KMX_KNX_INTEROP_PORT", "");
        if (port.empty())
            SKIP("KMX_KNX_INTEROP_PORT is unset: script/feature/knx/interop/run-secure-routing-interop.sh starts a peer and sets it");

        // The router is configured the way an application would be: from the ETS keyring the peer uses too.
        const auto keyring_path =
            detail::environment("KMX_KNX_INTEROP_KEYRING", (sv::conformance_directory() / "keyrings" / "keyring.knxkeys").string());
        std::ifstream input(keyring_path, std::ios::binary);
        const std::string xml {std::istreambuf_iterator<char> {input}, std::istreambuf_iterator<char> {}};
        const auto keyring = kn::keyring::load(xml, detail::environment("KMX_KNX_INTEROP_PASSWORD", "pwd"));
        REQUIRE(keyring.has_value());
        auto settings = kn::keyring::routing_configuration_for(*keyring, detail::serial);
        REQUIRE(settings.has_value());

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
        REQUIRE(router.start().has_value());

        detail::outcome state {};
        executor.spawn(detail::notify(router, state, executor));
        executor.spawn(detail::send(router, state, executor));
        executor.spawn(detail::receive(router, state, executor));
        detail::run_bounded(executor, std::chrono::seconds {std::stoul(detail::environment("KMX_KNX_INTEROP_TIMEOUT", "40"))});

        const auto& counters = router.secure_counters();
        INFO("sent=" << state.sent << " received=" << state.received << " authentication_failures=" << counters.authentication_failures
                     << " replays=" << counters.replays << " duplicates=" << counters.duplicates
                     << " notifications=" << counters.timer_notifications_sent << " adjustments=" << counters.timer_adjustments
                     << " synchronised=" << router.timer_synchronised() << " reflected=" << router.counters().reflected_messages);
        CHECK(state.answered);
        CHECK(counters.authentication_failures == 0u);
        CHECK(counters.unencrypted_refused == 0u);
    }
}
