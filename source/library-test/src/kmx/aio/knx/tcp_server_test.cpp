/// @file src/kmx/aio/knx/tcp_server_test.cpp
/// @brief KNXnet/IP tunnelling over TCP end to end: the in-tree client against the in-tree server, on both pillars.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details Real loopback connections throughout. The client opens its tunnel over a TCP transport, the server's accept
/// loop serves the connection on a task of its own, and frames cross both ways with a heartbeat between them. Then the
/// tunnel ends - politely, or by its connection going away - and the server has to hand the channel back either way.
#ifndef PCH
    #include <kmx/aio/completion/knx/tcp_server.hpp>

    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/completion/knx/tcp_transport.hpp>
    #include <kmx/aio/completion/timer.hpp>
    #include <kmx/aio/knx/generic_server.hpp>
    #include <kmx/aio/knx/server.hpp>
    #include <kmx/aio/knx/tunnelling_client.hpp>
    #include <kmx/aio/test/knx/telegram.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <algorithm>
    #include <atomic>
    #include <chrono>
    #include <cstdint>
    #include <memory>
    #include <mutex>
    #include <thread>
    #include <vector>
    #include <netinet/in.h>

    #if defined(KMX_AIO_FEATURE_READINESS)
        #include <kmx/aio/readiness/executor.hpp>
        #include <kmx/aio/readiness/knx/tcp_server.hpp>
        #include <kmx/aio/readiness/knx/tcp_transport.hpp>
    #endif
#endif

namespace kmx::aio::test::knx::tcp_server_test
{
    using namespace kmx::aio::knx;

    namespace detail
    {
        /// @brief How long a test waits for the other side before calling what it waits for lost.
        constexpr std::chrono::milliseconds patience {5'000};

        /// @brief A request for a link-layer tunnel; over TCP the client replaces its endpoints with the TCP HPAI.
        const connect_request_frame request {};

        /// @brief The frames the server's connections tunnelled in, gathered from whichever threads served them.
        class event_log
        {
        public:
            void record(server_event event)
            {
                const std::lock_guard lock {mutex_};
                events_.push_back(std::move(event));
            }

            [[nodiscard]] std::vector<server_event> events() const
            {
                const std::lock_guard lock {mutex_};
                return events_;
            }

        private:
            mutable std::mutex mutex_ {};
            std::vector<server_event> events_ {};
        };

        /// @brief What one client saw of its tunnel.
        struct tunnel_observation
        {
            bool connected {};
            std::uint8_t channel {};
            bool held {};
            bool sent {};
            bool answered {};
            bool beat {};
            bool answered_after_beat {};
            bool quiet_poll {};
            bool disconnected {};
        };

        [[nodiscard]] server_event_handler recorder(event_log& log)
        {
            return [&log](server_event event) -> task<void>
            {
                log.record(std::move(event));
                co_return;
            };
        }

        [[nodiscard]] sockaddr_in loopback(const port_t port) noexcept
        {
            sockaddr_in address {};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons(port);
            return address;
        }

        /// @brief Opens a tunnel, exchanges frames both ways around a heartbeat, and disconnects.
        template <typename Transport>
        task<void> run_tunnel(Transport& transport, const sockaddr_in& address, generic_server& server, tunnel_observation& observed)
        {
            tunnelling_client client {transport, reinterpret_cast<const sockaddr*>(&address), sizeof(address)};
            if (!(co_await client.connect(request)).has_value())
                co_return;
            observed.connected = true;
            observed.channel = client.channel_id();
            observed.sent = (co_await client.send(sample_cemi)).has_value();

            // The server's frames reach the client through its receive, and so does the answer to the heartbeat.
            static_cast<void>(co_await server.send(observed.channel, sample_cemi_read));
            const auto answer = co_await client.receive_cemi();
            observed.answered = answer.has_value() && std::ranges::equal(*answer, sample_cemi_read);
            observed.beat = (co_await client.heartbeat()).has_value();
            static_cast<void>(co_await server.send(observed.channel, sample_cemi));
            const auto second = co_await client.receive_cemi();
            observed.answered_after_beat = second.has_value() && std::ranges::equal(*second, sample_cemi);
            observed.quiet_poll = client.poll().has_value();
            observed.disconnected = (co_await client.disconnect()).has_value();
        }

        /// @brief Opens a tunnel, then drops the connection under it without disconnecting.
        template <typename Transport>
        task<void> abandon_tunnel(Transport& transport, const sockaddr_in& address, generic_server& server, tunnel_observation& observed)
        {
            tunnelling_client client {transport, reinterpret_cast<const sockaddr*>(&address), sizeof(address)};
            observed.connected = (co_await client.connect(request)).has_value();
            observed.channel = client.channel_id();
            observed.held = server.channel_active(observed.channel);
            client.shutdown();
        }

        /// @brief Opens a tunnel, sends @p frames frames down it, and disconnects.
        template <typename Transport>
        task<void> run_burst(Transport& transport, const sockaddr_in& address, const std::size_t frames, std::atomic_size_t& completed)
        {
            tunnelling_client client {transport, reinterpret_cast<const sockaddr*>(&address), sizeof(address)};
            if (!(co_await client.connect(request)).has_value())
                co_return;
            std::size_t sent {};
            for (std::size_t index {}; index < frames; ++index)
                if ((co_await client.send(sample_cemi)).has_value())
                    ++sent;
            const auto disconnected = co_await client.disconnect();
            if ((sent == frames) && disconnected.has_value())
                completed.fetch_add(1u);
        }

        /// @brief Runs an accept loop, then records that it ended.
        template <typename Server>
        task<void> run_accept_loop(Server& tcp, std::atomic_bool& ended)
        {
            static_cast<void>(co_await tcp.serve());
            ended = true;
        }

        /// @brief Runs @p work, then counts it finished.
        task<void> counted(task<void> work, std::atomic_size_t& finished)
        {
            co_await std::move(work);
            finished.fetch_add(1u);
        }

        /// @brief Waits on this thread until @p done holds, for no longer than @ref patience.
        template <typename Predicate>
        [[nodiscard]] bool wait_until(Predicate done)
        {
            const auto give_up = std::chrono::steady_clock::now() + patience;
            while (!done())
            {
                if (std::chrono::steady_clock::now() >= give_up)
                    return false;
                std::this_thread::sleep_for(std::chrono::milliseconds {5});
            }

            return true;
        }

        /// @brief Waits on the executor until @p done holds, for no longer than @ref patience.
        template <typename Predicate>
        task<bool> settle(completion::executor& executor, Predicate done)
        {
            for (std::chrono::milliseconds waited {}; !done(); waited += std::chrono::milliseconds {5})
            {
                if (waited >= patience)
                    co_return false;
                completion::timer timer {executor};
                static_cast<void>(co_await timer.wait(std::chrono::milliseconds {5}));
            }

            co_return true;
        }

        void check_tunnel(const tunnel_observation& observed, const event_log& log)
        {
            CHECK(observed.connected);
            CHECK(observed.channel != 0u);
            CHECK(observed.sent);
            CHECK(observed.answered);
            CHECK(observed.beat);
            CHECK(observed.answered_after_beat);
            CHECK(observed.quiet_poll);
            CHECK(observed.disconnected);
            // Only the client's frame reaches the handler: connection management yields no cEMI to hand over.
            const auto events = log.events();
            REQUIRE(events.size() == 1u);
            CHECK(events.front().channel_id == observed.channel);
            CHECK(std::ranges::equal(events.front().cemi_bytes, sample_cemi));
        }
    }

    TEST_CASE("knx completion tcp server tunnels a client end to end", "[knx][tcp][integration][completion]")
    {
        completion::executor executor;
        generic_server server {server_config {.first_assigned_address = individual_address {1u, 1u, 50u}}};
        detail::event_log log {};
        completion::knx::tcp_server tcp {
            executor, server, {.bind_address = {127u, 0u, 0u, 1u}, .port = 0u, .on_event = detail::recorder(log)}};
        REQUIRE(tcp.listen().has_value());
        const auto address = detail::loopback(tcp.port());
        completion::knx::tcp_transport transport {executor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)};
        detail::tunnel_observation observed {};
        std::atomic_bool serving_ended {};
        bool released {};
        bool stopped {};

        auto run = [&]() -> task<void>
        {
            executor.spawn(detail::run_accept_loop(tcp, serving_ended));
            co_await detail::run_tunnel(transport, address, server, observed);
            released = co_await detail::settle(executor, [&]() { return (server.active_channels() == 0u) && (tcp.connections() == 0u); });
            tcp.stop();
            stopped = co_await detail::settle(executor, [&]() { return serving_ended.load(); });
            executor.stop();
        };
        executor.spawn(run());
        executor.run();

        detail::check_tunnel(observed, log);
        CHECK(released);
        CHECK(stopped);
    }

    TEST_CASE("knx completion tcp server releases a channel when its connection goes", "[knx][tcp][integration][completion]")
    {
        completion::executor executor;
        generic_server server {server_config {}};
        completion::knx::tcp_server tcp {executor, server, {.bind_address = {127u, 0u, 0u, 1u}, .port = 0u}};
        REQUIRE(tcp.listen().has_value());
        const auto address = detail::loopback(tcp.port());
        completion::knx::tcp_transport transport {executor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)};
        detail::tunnel_observation observed {};
        std::atomic_bool serving_ended {};
        bool released {};
        bool stopped {};

        auto run = [&]() -> task<void>
        {
            executor.spawn(detail::run_accept_loop(tcp, serving_ended));
            co_await detail::abandon_tunnel(transport, address, server, observed);
            released = co_await detail::settle(executor, [&]() { return (server.active_channels() == 0u) && (tcp.connections() == 0u); });
            tcp.stop();
            stopped = co_await detail::settle(executor, [&]() { return serving_ended.load(); });
            executor.stop();
        };
        executor.spawn(run());
        executor.run();

        CHECK(observed.connected);
        CHECK(observed.held);
        CHECK(released);
        CHECK(stopped);
    }

#if defined(KMX_AIO_FEATURE_READINESS)
    TEST_CASE("knx readiness tcp server tunnels a client end to end", "[knx][tcp][integration][readiness]")
    {
        auto executor = std::make_shared<readiness::executor>(readiness::executor_config {.thread_count = 2u, .timeout_ms = 20u});
        generic_server server {server_config {.first_assigned_address = individual_address {1u, 1u, 50u}}};
        detail::event_log log {};
        readiness::knx::tcp_server tcp {
            *executor, server, {.bind_address = {127u, 0u, 0u, 1u}, .port = 0u, .on_event = detail::recorder(log)}};
        REQUIRE(tcp.listen().has_value());
        const auto address = detail::loopback(tcp.port());
        readiness::knx::tcp_transport transport {*executor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)};
        detail::tunnel_observation observed {};
        std::atomic_bool serving_ended {};
        std::atomic_size_t finished {};

        executor->spawn(detail::run_accept_loop(tcp, serving_ended));
        executor->spawn(detail::counted(detail::run_tunnel(transport, address, server, observed), finished));
        std::jthread runner([executor]() { executor->run(); });
        CHECK(detail::wait_until([&]() { return finished.load() == 1u; }));
        CHECK(detail::wait_until([&]() { return (server.active_channels() == 0u) && (tcp.connections() == 0u); }));
        tcp.stop();
        CHECK(detail::wait_until([&]() { return serving_ended.load(); }));
        executor->stop();
        runner.join();

        detail::check_tunnel(observed, log);
    }

    TEST_CASE("knx readiness tcp server releases a channel when its connection goes", "[knx][tcp][integration][readiness]")
    {
        auto executor = std::make_shared<readiness::executor>(readiness::executor_config {.thread_count = 2u, .timeout_ms = 20u});
        generic_server server {server_config {}};
        readiness::knx::tcp_server tcp {*executor, server, {.bind_address = {127u, 0u, 0u, 1u}, .port = 0u}};
        REQUIRE(tcp.listen().has_value());
        const auto address = detail::loopback(tcp.port());
        readiness::knx::tcp_transport transport {*executor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)};
        detail::tunnel_observation observed {};
        std::atomic_bool serving_ended {};
        std::atomic_size_t finished {};

        executor->spawn(detail::run_accept_loop(tcp, serving_ended));
        executor->spawn(detail::counted(detail::abandon_tunnel(transport, address, server, observed), finished));
        std::jthread runner([executor]() { executor->run(); });
        CHECK(detail::wait_until([&]() { return finished.load() == 1u; }));
        CHECK(detail::wait_until([&]() { return (server.active_channels() == 0u) && (tcp.connections() == 0u); }));
        tcp.stop();
        CHECK(detail::wait_until([&]() { return serving_ended.load(); }));
        executor->stop();
        runner.join();

        CHECK(observed.connected);
        CHECK(observed.held);
    }

    TEST_CASE("knx readiness tcp server serves several clients at once", "[knx][tcp][integration][readiness]")
    {
        constexpr std::size_t clients = 4u;
        constexpr std::size_t frames = 3u;
        auto executor = std::make_shared<readiness::executor>(readiness::executor_config {.thread_count = 2u, .timeout_ms = 20u});
        generic_server server {server_config {.max_channels = clients}};
        std::atomic_size_t delivered {};
        auto count_delivered = [&delivered](server_event) -> task<void>
        {
            delivered.fetch_add(1u);
            co_return;
        };
        readiness::knx::tcp_server tcp {*executor, server, {.bind_address = {127u, 0u, 0u, 1u}, .port = 0u, .on_event = count_delivered}};
        REQUIRE(tcp.listen().has_value());
        const auto address = detail::loopback(tcp.port());
        std::vector<std::unique_ptr<readiness::knx::tcp_transport>> transports {};
        std::atomic_size_t completed {};
        std::atomic_size_t finished {};
        std::atomic_bool serving_ended {};

        executor->spawn(detail::run_accept_loop(tcp, serving_ended));
        for (std::size_t index {}; index < clients; ++index)
        {
            transports.push_back(
                std::make_unique<readiness::knx::tcp_transport>(*executor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)));
            executor->spawn(detail::counted(detail::run_burst(*transports.back(), address, frames, completed), finished));
        }

        std::jthread runner([executor]() { executor->run(); });
        CHECK(detail::wait_until([&]() { return finished.load() == clients; }));
        CHECK(detail::wait_until([&]() { return (server.active_channels() == 0u) && (tcp.connections() == 0u); }));
        tcp.stop();
        CHECK(detail::wait_until([&]() { return serving_ended.load(); }));
        executor->stop();
        runner.join();

        CHECK(completed.load() == clients);
        CHECK(delivered.load() == clients * frames);
    }
#endif
}
