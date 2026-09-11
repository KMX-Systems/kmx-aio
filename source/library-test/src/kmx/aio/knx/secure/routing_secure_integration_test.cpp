/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/knx/udp_transport.hpp>
#include <kmx/aio/completion/udp/endpoint.hpp>
#include <kmx/aio/file_descriptor.hpp>
#include <kmx/aio/knx/routing.hpp>
#if defined(KMX_AIO_FEATURE_READINESS)
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/readiness/knx/udp_transport.hpp>
    #include <kmx/aio/readiness/udp/endpoint.hpp>
#endif
#include <kmx/aio/test/knx/secure_vectors.hpp>
#include <kmx/aio/test/knx/telegram.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <net/if.h>
#include <netinet/in.h>
#include <stop_token>
#include <string_view>
#include <thread>

namespace kmx::aio::test::knx::secure::routing_secure_integration_test
{
    namespace kr = kmx::aio::knx::routing;
    namespace sv = kmx::aio::test::knx::secure_vectors;

    namespace detail
    {
        /// @brief The secure clock the routers of a test share: well away from zero, and moved only by the test.
        std::atomic<std::uint64_t> clock_ms {10'000'000u};

        [[nodiscard]] std::uint64_t clock() noexcept
        {
            return clock_ms.load(std::memory_order_relaxed);
        }

        inline constexpr std::string_view backbone_key = "96f034fccf510760cbd63da0f70d4a9d";

        [[nodiscard]] port_t reserve_udp_port() noexcept(false)
        {
            auto socket = file_descriptor::create_socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
            REQUIRE(socket.has_value());
            sockaddr_in local {};
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            REQUIRE(socket->bind(reinterpret_cast<const sockaddr*>(&local), sizeof(local)).has_value());
            sockaddr_in bound {};
            socklen_t length = sizeof(bound);
            REQUIRE(::getsockname(socket->get(), reinterpret_cast<sockaddr*>(&bound), &length) == 0);
            return ntohs(bound.sin_port);
        }

        /// @brief The routing group on the loopback interface, with loopback on so that two routers in one process
        ///        hear each other.
        [[nodiscard]] kr::multicast_configuration loopback_group() noexcept(false)
        {
            const auto index = ::if_nametoindex("lo");
            REQUIRE(index != 0u);
            return {.group = {224u, 0u, 23u, 12u}, .port = reserve_udp_port(), .interface_index = index, .loopback = true};
        }

        [[nodiscard]] kr::secure_configuration settings(const std::uint8_t router) noexcept(false)
        {
            kr::secure_configuration value {};
            value.backbone_key = sv::key(backbone_key);
            value.serial_number = {0x00u, 0x00u, 0x6Bu, 0x6Du, 0x78u, router};
            return value;
        }

        /// @brief The switch-off counterpart of the sample telegram, so the two directions carry different frames.
        [[nodiscard]] sv::octets_t switch_off() noexcept(false)
        {
            sv::octets_t cemi(sample_cemi.begin(), sample_cemi.end());
            cemi.back() = 0x80u;
            return cemi;
        }

        [[nodiscard]] bool carries(const kr::received_indication_result_t& received, const cspan_uint8_t cemi) noexcept
        {
            return received.has_value() && std::ranges::equal(received->cemi_bytes.span(), cemi);
        }

        /// @brief Runs an executor on this thread, and stops it should the scenario not finish within @p limit, so a
        ///        receive that never completes fails the test instead of hanging it.
        template <typename Executor>
        void run_bounded(Executor& executor, const std::chrono::seconds limit) noexcept(false)
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

        /// @brief Both routers send their synchronisation request. Their clocks agree, so neither answers, and once the
        ///        wait runs out on the shared clock both keep the time.
        task<bool> synchronise(kr::client& first, kr::client& second) noexcept(false)
        {
            auto sent = (co_await first.notify_timer()).has_value();
            sent = (co_await second.notify_timer()).has_value() && sent;
            clock_ms.fetch_add(3'300u, std::memory_order_relaxed);
            sent = (co_await first.notify_timer()).has_value() && sent;
            sent = (co_await second.notify_timer()).has_value() && sent;
            const auto synchronised = first.timer_synchronised() && second.timer_synchronised();
            co_return sent && synchronised;
        }

        /// @brief How many frames the concurrent scenario has the peer send.
        inline constexpr std::size_t concurrent_frames = 64u;

        /// @brief What the concurrent scenario observed; atomics, because its tasks run on two threads.
        struct concurrent_outcome
        {
            std::atomic<bool> synchronised {};
            std::atomic<std::size_t> sent {};
            std::atomic<std::size_t> delivered {};
            std::atomic<std::size_t> own_delivered {};
            std::atomic<bool> receiver_done {};
            std::atomic<bool> sender_done {};
        };

#if defined(KMX_AIO_FEATURE_READINESS)
        /// @brief Receives on the router until every frame the peer sends has been delivered.
        task<void> receive_all(kr::client& router, concurrent_outcome& outcome, readiness::executor& executor) noexcept(false)
        {
            const auto off_cemi = switch_off();
            while (outcome.delivered.load() < concurrent_frames)
            {
                const auto received = co_await router.receive_indication();
                if (!received.has_value())
                    break;
                outcome.delivered.fetch_add(carries(received, off_cemi) ? 1u : 0u);
                outcome.own_delivered.fetch_add(carries(received, sample_cemi) ? 1u : 0u);
            }
            // Whichever task finishes last stops the executor, so nothing is read while the other still runs.
            outcome.receiver_done.store(true);
            if (outcome.sender_done.load())
                executor.stop();
        }

        /// @brief Synchronises both routers, then has the peer send to the router while the router, in this same task,
        ///        notifies and sends on its own - concurrently with the task receiving on it.
        task<void> notify_and_send(kr::client& router, kr::client& peer, concurrent_outcome& outcome,
                                   readiness::executor& executor) noexcept(false)
        {
            outcome.synchronised.store(co_await synchronise(router, peer));
            const kr::indication on {sample_cemi};
            const auto off_cemi = switch_off();
            const kr::indication off {off_cemi};
            for (std::size_t frame {}; frame < concurrent_frames; ++frame)
            {
                outcome.sent.fetch_add((co_await peer.send_indication(off)).has_value() ? 1u : 0u);
                (void) co_await router.notify_timer();
                (void) co_await router.send_indication(on);
            }
            outcome.sender_done.store(true);
            if (outcome.receiver_done.load())
                executor.stop();
        }
#endif

        /// @brief What the two-router exchange observed.
        struct exchange
        {
            bool synchronised {};
            bool first_to_second {};
            bool second_to_first {};
        };

        /// @brief Each router sends one wrapped indication, and the other must deliver it.
        template <typename Executor>
        task<void> exchange_indications(kr::client& first, kr::client& second, exchange& outcome, Executor& executor) noexcept(false)
        {
            outcome.synchronised = co_await synchronise(first, second);
            const kr::indication on {sample_cemi};
            const auto off_cemi = switch_off();
            const kr::indication off {off_cemi};

            if ((co_await first.send_indication(on)).has_value())
                outcome.first_to_second = carries(co_await second.receive_indication(), sample_cemi);
            if ((co_await second.send_indication(off)).has_value())
                outcome.second_to_first = carries(co_await first.receive_indication(), off_cemi);
            executor.stop();
        }

        /// @brief Checks what both routers counted after a clean exchange.
        void check_counters(const kr::client& first, const kr::client& second) noexcept(false)
        {
            for (const auto* const router: {&first, &second})
            {
                CHECK(router->secure_counters().authentication_failures == 0u);
                CHECK(router->secure_counters().unencrypted_refused == 0u);
                CHECK(router->secure_counters().refused_services == 0u);
            }
        }
    } // namespace detail

    TEST_CASE("knx secure routers exchange wrapped indications over loopback multicast", "[knx][secure][routing][completion][integration]")
    {
        const auto group = detail::loopback_group();
        completion::executor executor;
        auto first_endpoint = completion::udp::endpoint::create(executor, AF_INET);
        auto second_endpoint = completion::udp::endpoint::create(executor, AF_INET);
        REQUIRE(first_endpoint.has_value());
        REQUIRE(second_endpoint.has_value());
        completion::knx::udp_transport first_transport {*first_endpoint};
        completion::knx::udp_transport second_transport {*second_endpoint};
        kr::client first {first_transport, group, detail::settings(1u), detail::clock};
        kr::client second {second_transport, group, detail::settings(2u), detail::clock};
        REQUIRE(first.start().has_value());
        REQUIRE(second.start().has_value());

        detail::exchange outcome {};
        executor.spawn(detail::exchange_indications(first, second, outcome, executor));
        detail::run_bounded(executor, std::chrono::seconds {10});

        CHECK(outcome.synchronised);
        CHECK(outcome.first_to_second);
        CHECK(outcome.second_to_first);
        detail::check_counters(first, second);
        CHECK(first.stop().has_value());
        CHECK(second.stop().has_value());
    }

#if defined(KMX_AIO_FEATURE_READINESS)
    TEST_CASE("knx secure routers exchange wrapped indications on the readiness pillar", "[knx][secure][routing][readiness][integration]")
    {
        const auto group = detail::loopback_group();
        auto executor = std::make_shared<readiness::executor>(readiness::executor_config {.thread_count = 1u, .timeout_ms = 20u});
        auto first_endpoint = readiness::udp::endpoint::create(*executor, AF_INET);
        auto second_endpoint = readiness::udp::endpoint::create(*executor, AF_INET);
        REQUIRE(first_endpoint.has_value());
        REQUIRE(second_endpoint.has_value());
        readiness::knx::udp_transport first_transport {*first_endpoint};
        readiness::knx::udp_transport second_transport {*second_endpoint};
        kr::client first {first_transport, group, detail::settings(3u), detail::clock};
        kr::client second {second_transport, group, detail::settings(4u), detail::clock};
        REQUIRE(first.start().has_value());
        REQUIRE(second.start().has_value());

        detail::exchange outcome {};
        executor->spawn(detail::exchange_indications(first, second, outcome, *executor));
        detail::run_bounded(*executor, std::chrono::seconds {10});

        CHECK(outcome.synchronised);
        CHECK(outcome.first_to_second);
        CHECK(outcome.second_to_first);
        detail::check_counters(first, second);
        CHECK(first.stop().has_value());
        CHECK(second.stop().has_value());
    }

    TEST_CASE("knx secure router receives while another task notifies and sends, on two threads",
              "[knx][secure][routing][readiness][integration][concurrency]")
    {
        const auto group = detail::loopback_group();
        auto executor = std::make_shared<readiness::executor>(readiness::executor_config {.thread_count = 2u, .timeout_ms = 20u});
        auto router_endpoint = readiness::udp::endpoint::create(*executor, AF_INET);
        auto peer_endpoint = readiness::udp::endpoint::create(*executor, AF_INET);
        REQUIRE(router_endpoint.has_value());
        REQUIRE(peer_endpoint.has_value());
        readiness::knx::udp_transport router_transport {*router_endpoint};
        readiness::knx::udp_transport peer_transport {*peer_endpoint};
        kr::client router {router_transport, group, detail::settings(5u), detail::clock};
        kr::client peer {peer_transport, group, detail::settings(6u), detail::clock};
        REQUIRE(router.start().has_value());
        REQUIRE(peer.start().has_value());

        detail::concurrent_outcome outcome {};
        executor->spawn(detail::receive_all(router, outcome, *executor));
        executor->spawn(detail::notify_and_send(router, peer, outcome, *executor));
        detail::run_bounded(*executor, std::chrono::seconds {20});

        CHECK(outcome.synchronised.load());
        CHECK(outcome.sent.load() == detail::concurrent_frames);
        CHECK(outcome.delivered.load() == detail::concurrent_frames);
        CHECK(outcome.own_delivered.load() == 0u);
        CHECK(router.secure_counters().authentication_failures == 0u);
        CHECK(router.secure_counters().replays == 0u);
    }
#endif
}
