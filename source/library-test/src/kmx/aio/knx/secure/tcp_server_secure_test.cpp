/// @file kmx/aio/knx/secure/tcp_server_secure_test.cpp
/// @brief KNX IP Secure tunnelling end to end: the in-tree secure client against the in-tree secure server, on both pillars.
/// @details Real loopback connections throughout. The server holds two users - one with four tunnel addresses, one with a
/// single address - and clients authenticate, tunnel both ways, and disconnect. Around that: a wrong password, an unknown
/// user, another user's address, a plain client that must not get a channel (P1), a handshake abandoned half way, a third
/// handshake from one address turned away, a connection that never opens a session, several clients at once on two
/// threads, and a server torn down while a session is still open.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/knx/tcp_server.hpp>
#include <kmx/aio/completion/knx/tcp_transport.hpp>
#include <kmx/aio/completion/timer.hpp>
#include <kmx/aio/file_descriptor.hpp>
#include <kmx/aio/knx/client.hpp>
#include <kmx/aio/knx/error.hpp>
#include <kmx/aio/knx/secure/client_session.hpp>
#include <kmx/aio/knx/server.hpp>
#include <kmx/aio/test/knx/telegram.hpp>
#if defined(KMX_AIO_FEATURE_READINESS)
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/readiness/knx/tcp_server.hpp>
    #include <kmx/aio/readiness/knx/tcp_transport.hpp>
    #include <kmx/aio/readiness/timer.hpp>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <vector>

namespace kmx::aio::test::knx::secure::tcp_server_secure_test
{
    namespace kn = kmx::aio::knx;
    namespace ks = kmx::aio::knx::secure;
    using kn::error;
    using kn::make_error_code;

    namespace detail
    {
        /// @brief How long a test waits for the other side before calling what it waits for lost.
        constexpr std::chrono::milliseconds patience {10'000};

        constexpr ks::serial_number_t client_serial {0x00u, 0xFAu, 0x12u, 0x34u, 0x56u, 0x78u};
        constexpr ks::serial_number_t server_serial {0x00u, 0xFAu, 0xAAu, 0xAAu, 0xAAu, 0xAAu};
        /// @brief The user with four tunnel addresses, and the user with one.
        constexpr std::uint8_t first_user = 2u;
        constexpr std::uint8_t second_user = 3u;

        [[nodiscard]] ks::secret_key derived_user_key(const std::string_view password)
        {
            auto key = ks::derive_user_password_key(password);
            REQUIRE(key.has_value());
            return std::move(*key);
        }

        // Each key is derived once: PBKDF2 is where these tests would otherwise spend their time.
        [[nodiscard]] const ks::secret_key& first_password()
        {
            static const auto key = derived_user_key("secret");
            return key;
        }

        [[nodiscard]] const ks::secret_key& second_password()
        {
            static const auto key = derived_user_key("other");
            return key;
        }

        [[nodiscard]] const ks::secret_key& wrong_password()
        {
            static const auto key = derived_user_key("not the password");
            return key;
        }

        [[nodiscard]] const ks::secret_key& device_code()
        {
            static const auto key = []
            {
                auto derived = ks::derive_device_authentication_code("trustme");
                REQUIRE(derived.has_value());
                return std::move(*derived);
            }();
            return key;
        }

        /// @brief A secure server configuration.
        /// @param lifetime_ms How long a handshake, and a connection without a session, may take.
        /// @param handshakes_per_peer How many handshakes one address may have under way; every client here is on loopback.
        [[nodiscard]] kn::server_config secure_config(const std::uint32_t lifetime_ms = 10'000u,
                                                      const std::uint8_t handshakes_per_peer = 2u)
        {
            auto secure = std::make_shared<ks::server_configuration>();
            secure->device_authentication_code = device_code().clone();
            secure->users.push_back(
                ks::tunnelling_user {.user_id = first_user,
                                     .password_key = first_password().clone(),
                                     .tunnel_addresses = {kn::individual_address {1u, 1u, 240u}, kn::individual_address {1u, 1u, 241u},
                                                          kn::individual_address {1u, 1u, 242u}, kn::individual_address {1u, 1u, 243u}}});
            secure->users.push_back(ks::tunnelling_user {.user_id = second_user,
                                                         .password_key = second_password().clone(),
                                                         .tunnel_addresses = {kn::individual_address {1u, 1u, 250u}}});
            secure->serial_number = server_serial;
            secure->unauthenticated_lifetime_ms = lifetime_ms;
            secure->max_unauthenticated_per_peer = handshakes_per_peer;
            return kn::server_config {.max_channels = 8u, .secure = std::move(secure)};
        }

        [[nodiscard]] ks::tunnelling_credentials credentials(const std::uint8_t user_id, const ks::secret_key& password)
        {
            return ks::tunnelling_credentials {.user_id = user_id,
                                               .user_password_key = password.clone(),
                                               .device_authentication_code = device_code().clone(),
                                               .serial_number = client_serial};
        }

        /// @brief A request for a link-layer tunnel, naming the address to tunnel under when @p address is set.
        [[nodiscard]] kn::connect_request_frame request(const std::optional<kn::individual_address> address = std::nullopt)
        {
            return kn::connect_request_frame {.requested_address = address};
        }

        /// @brief The frames the server's connections tunnelled in, gathered from whichever threads served them.
        class event_log
        {
        public:
            void record(kn::server_event event)
            {
                const std::lock_guard lock {mutex_};
                events_.push_back(std::move(event));
            }

            [[nodiscard]] std::vector<kn::server_event> events() const
            {
                const std::lock_guard lock {mutex_};
                return events_;
            }

        private:
            mutable std::mutex mutex_ {};
            std::vector<kn::server_event> events_ {};
        };

        [[nodiscard]] kn::server_event_handler recorder(event_log& log)
        {
            return [&log](kn::server_event event) -> task<void>
            {
                log.record(std::move(event));
                co_return;
            };
        }

        /// @brief What one secure client saw of its tunnel.
        struct tunnel_observation
        {
            bool connected {};
            std::uint8_t channel {};
            kn::individual_address assigned {};
            bool sent {};
            bool answered {};
            bool beat {};
            bool disconnected {};
            std::error_code refusal {};
        };

        [[nodiscard]] sockaddr_in loopback(const port_t port) noexcept
        {
            sockaddr_in address {};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons(port);
            return address;
        }

        /// @brief Opens a secure tunnel, exchanges frames both ways around a heartbeat, and disconnects.
        template <typename Transport>
        task<void> run_secure_tunnel(Transport& transport, const sockaddr_in& address, kn::generic_server& server,
                                     ks::tunnelling_credentials credentials, const kn::connect_request_frame connect_request,
                                     tunnel_observation& observed)
        {
            kn::tunnelling_client client {transport, reinterpret_cast<const sockaddr*>(&address), sizeof(address), kn::tunnelling_config {},
                                          std::move(credentials)};
            if (const auto connected = co_await client.connect(connect_request); !connected.has_value())
            {
                observed.refusal = connected.error();
                co_return;
            }
            observed.connected = true;
            observed.channel = client.channel_id();
            observed.assigned = client.assigned_address();
            observed.sent = (co_await client.send(sample_cemi)).has_value();
            static_cast<void>(co_await server.send(observed.channel, sample_cemi_read));
            const auto answer = co_await client.receive_cemi();
            observed.answered = answer.has_value() && std::ranges::equal(*answer, sample_cemi_read);
            observed.beat = (co_await client.heartbeat()).has_value();
            observed.disconnected = (co_await client.disconnect()).has_value();
        }

        /// @brief Opens a plain tunnel to the secure server.
        template <typename Transport>
        task<void> run_plain_tunnel(Transport& transport, const sockaddr_in& address, tunnel_observation& observed)
        {
            kn::tunnelling_client client {transport, reinterpret_cast<const sockaddr*>(&address), sizeof(address)};
            const auto connected = co_await client.connect(request());
            observed.connected = connected.has_value();
            observed.refusal = connected.has_value() ? std::error_code {} : connected.error();
        }

        /// @brief Opens a secure tunnel, sends @p frames frames down it, and disconnects.
        template <typename Transport>
        task<void> run_secure_burst(Transport& transport, const sockaddr_in& address, const std::size_t frames,
                                    std::atomic_size_t& completed)
        {
            kn::tunnelling_client client {transport, reinterpret_cast<const sockaddr*>(&address), sizeof(address), kn::tunnelling_config {},
                                          credentials(first_user, first_password())};
            if (!(co_await client.connect(request())).has_value())
                co_return;
            std::size_t sent {};
            for (std::size_t index {}; index < frames; ++index)
            {
                if ((co_await client.send(sample_cemi)).has_value())
                    ++sent;
            }
            const auto disconnected = co_await client.disconnect();
            if ((sent == frames) && disconnected.has_value())
                completed.fetch_add(1u);
        }

        /// @brief Runs an accept loop, then records that it ended.
        template <typename Server>
        task<void> run_server(Server& tcp, std::atomic_bool& ended)
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

        /// @brief Connects a blocking loopback socket to @p port.
        [[nodiscard]] file_descriptor connect_blocking(const port_t port) noexcept
        {
            file_descriptor connection {::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
            const auto address = loopback(port);
            if (!connection.is_valid() || (::connect(connection.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0))
                return file_descriptor {};
            return connection;
        }

        [[nodiscard]] bool write_all(const file_descriptor& connection, std::span<const std::uint8_t> octets) noexcept
        {
            while (!octets.empty())
            {
                const auto written = ::send(connection.get(), octets.data(), octets.size(), MSG_NOSIGNAL);
                if (written <= 0)
                    return false;
                octets = octets.subspan(static_cast<std::size_t>(written));
            }
            return true;
        }

        [[nodiscard]] std::vector<std::uint8_t> read_exactly(const file_descriptor& connection, const std::size_t count)
        {
            std::vector<std::uint8_t> octets(count, 0u);
            for (std::size_t filled {}; filled < count;)
            {
                ::pollfd waiting {connection.get(), POLLIN, 0};
                if (::poll(&waiting, 1u, static_cast<int>(patience.count())) != 1)
                    return {};
                const auto received = ::recv(connection.get(), octets.data() + filled, count - filled, 0);
                if (received <= 0)
                    return {};
                filled += static_cast<std::size_t>(received);
            }
            return octets;
        }

        /// @brief A genuine SESSION_REQUEST, as a client opening a session over TCP sends it.
        [[nodiscard]] std::vector<std::uint8_t> session_request()
        {
            ks::client_session session {credentials(first_user, first_password()), ks::system_entropy()};
            std::vector<std::uint8_t> wire(ks::session_request_size, 0u);
            const auto size = session.begin(wire, kn::hpai {kn::ipv4_endpoint {}, 0x02u}, 0u);
            REQUIRE(size.has_value());
            wire.resize(*size);
            return wire;
        }

        void check_tunnel(const tunnel_observation& observed, const event_log& log)
        {
            CHECK(observed.connected);
            CHECK(observed.channel != 0u);
            CHECK(observed.assigned == kn::individual_address {1u, 1u, 240u});
            CHECK(observed.sent);
            CHECK(observed.answered);
            CHECK(observed.beat);
            CHECK(observed.disconnected);
            const auto events = log.events();
            REQUIRE(events.size() == 1u);
            CHECK(events.front().channel_id == observed.channel);
            CHECK(std::ranges::equal(events.front().cemi_bytes, sample_cemi));
        }

        /// @brief The clock secure sessions run on in the quiet-bus test, moved by hand.
        std::atomic_uint64_t session_clock_ms {};

        [[nodiscard]] std::uint64_t session_clock() noexcept
        {
            return session_clock_ms.load();
        }

#if defined(KMX_AIO_FEATURE_READINESS)
        /// @brief Suspends for @p milliseconds on the readiness executor.
        task<void> pause(readiness::executor& executor, const std::uint32_t milliseconds)
        {
            auto timer = readiness::timer::create();
            if (!timer.has_value() || !executor.register_fd(timer->get()).has_value())
                co_return;
            ::itimerspec when {};
            when.it_value.tv_sec = static_cast<::time_t>(milliseconds / 1'000u);
            when.it_value.tv_nsec = static_cast<long>(milliseconds % 1'000u) * 1'000'000L;
            if (timer->set_time(0, when).has_value())
                static_cast<void>(co_await timer->wait(executor));
            executor.unregister_fd(timer->get());
        }

        /// @brief What a session kept open on a quiet bus saw.
        struct quiet_observation
        {
            bool connected {};
            std::size_t kept_alive {};
            bool open_after {};
            bool sent {};
            bool disconnected {};
        };

        /// @brief Opens a session, lets five minutes of session time pass with nothing but keep-alives, then tunnels a frame.
        task<void> keep_quiet_session(readiness::executor& executor, kn::tunnelling_client& client, const kn::generic_server& server,
                                      quiet_observation& observed)
        {
            observed.connected = (co_await client.connect(request())).has_value();
            for (std::size_t round {}; observed.connected && (round < 6u); ++round)
            {
                session_clock_ms += 50'000u;
                if (client.keep_alive_due() && (co_await client.keep_alive()).has_value())
                    ++observed.kept_alive;
                // Long enough for the server to take the keep-alive before the clock moves again.
                co_await pause(executor, 150u);
            }
            observed.open_after = client.poll().has_value() && (server.secure_sessions() == 1u);
            observed.sent = (co_await client.send(sample_cemi)).has_value();
            observed.disconnected = (co_await client.disconnect()).has_value();
        }
#endif

        void check_session_closed(const kn::generic_server& server)
        {
            const auto counters = server.secure_counters();
            CHECK(counters.sessions_opened == 1u);
            CHECK(counters.sessions_closed == 1u);
            CHECK(counters.authentication_failures == 0u);
            CHECK(server.secure_sessions() == 0u);
        }
    }

    TEST_CASE("knx completion secure tcp server tunnels an authenticated client end to end",
              "[knx][secure][server][integration][completion]")
    {
        completion::executor executor;
        kn::generic_server server {detail::secure_config()};
        detail::event_log log {};
        completion::knx::tcp_server tcp {
            executor, server, {.bind_address = {127u, 0u, 0u, 1u}, .port = 0u, .on_event = detail::recorder(log)}};
        REQUIRE(tcp.listen().has_value());
        const auto address = detail::loopback(tcp.port());
        completion::knx::tcp_transport transport {executor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)};
        detail::tunnel_observation observed {};
        std::atomic_bool serving_ended {};
        bool released {};

        auto run = [&]() -> task<void>
        {
            executor.spawn(detail::run_server(tcp, serving_ended));
            co_await detail::run_secure_tunnel(
                transport, address, server, detail::credentials(detail::first_user, detail::first_password()), detail::request(), observed);
            released = co_await detail::settle(
                executor,
                [&]() { return (server.active_channels() == 0u) && (server.secure_sessions() == 0u) && (tcp.connections() == 0u); });
            tcp.stop();
            static_cast<void>(co_await detail::settle(executor, [&]() { return serving_ended.load(); }));
            executor.stop();
        };
        executor.spawn(run());
        executor.run();

        detail::check_tunnel(observed, log);
        CHECK(released);
        detail::check_session_closed(server);
    }

    TEST_CASE("knx completion secure tcp server refuses a wrong password, an unknown user and another user's address",
              "[knx][secure][server][integration][completion]")
    {
        completion::executor executor;
        kn::generic_server server {detail::secure_config()};
        completion::knx::tcp_server tcp {executor, server, {.bind_address = {127u, 0u, 0u, 1u}, .port = 0u}};
        REQUIRE(tcp.listen().has_value());
        const auto address = detail::loopback(tcp.port());
        std::array<detail::tunnel_observation, 4u> observed {};
        std::array<std::unique_ptr<completion::knx::tcp_transport>, 4u> transports {};
        for (auto& transport: transports)
            transport =
                std::make_unique<completion::knx::tcp_transport>(executor, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
        std::atomic_bool serving_ended {};
        bool released {};

        auto run = [&]() -> task<void>
        {
            executor.spawn(detail::run_server(tcp, serving_ended));
            const auto own_address = kn::individual_address {1u, 1u, 241u};
            const auto second_users_address = kn::individual_address {1u, 1u, 250u};
            co_await detail::run_secure_tunnel(*transports[0u], address, server,
                                               detail::credentials(detail::first_user, detail::wrong_password()), detail::request(),
                                               observed[0u]);
            co_await detail::run_secure_tunnel(*transports[1u], address, server, detail::credentials(9u, detail::first_password()),
                                               detail::request(), observed[1u]);
            co_await detail::run_secure_tunnel(*transports[2u], address, server,
                                               detail::credentials(detail::first_user, detail::first_password()),
                                               detail::request(second_users_address), observed[2u]);
            co_await detail::run_secure_tunnel(*transports[3u], address, server,
                                               detail::credentials(detail::first_user, detail::first_password()),
                                               detail::request(own_address), observed[3u]);
            released = co_await detail::settle(
                executor,
                [&]() { return (server.active_channels() == 0u) && (server.secure_sessions() == 0u) && (tcp.connections() == 0u); });
            tcp.stop();
            static_cast<void>(co_await detail::settle(executor, [&]() { return serving_ended.load(); }));
            executor.stop();
        };
        executor.spawn(run());
        executor.run();

        CHECK(observed[0u].refusal == make_error_code(error::secure_session_rejected));
        CHECK(observed[1u].refusal == make_error_code(error::secure_session_rejected));
        // Authenticated, but refused the channel: the address belongs to another user.
        CHECK(!observed[2u].connected);
        CHECK(observed[2u].refusal);
        CHECK(observed[3u].connected);
        CHECK(observed[3u].assigned == kn::individual_address {1u, 1u, 241u});
        CHECK(released);
        CHECK(server.secure_counters().authentication_failures == 2u);
    }

    TEST_CASE("knx completion secure tcp server refuses an unencrypted tunnel and allocates nothing for it",
              "[knx][secure][server][integration][completion]")
    {
        completion::executor executor;
        kn::generic_server server {detail::secure_config()};
        completion::knx::tcp_server tcp {executor, server, {.bind_address = {127u, 0u, 0u, 1u}, .port = 0u}};
        REQUIRE(tcp.listen().has_value());
        const auto address = detail::loopback(tcp.port());
        completion::knx::tcp_transport transport {executor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)};
        detail::tunnel_observation observed {};
        std::atomic_bool serving_ended {};
        bool channels_after {};

        auto run = [&]() -> task<void>
        {
            executor.spawn(detail::run_server(tcp, serving_ended));
            co_await detail::run_plain_tunnel(transport, address, observed);
            channels_after = server.active_channels() != 0u;
            static_cast<void>(co_await detail::settle(executor, [&]() { return tcp.connections() == 0u; }));
            tcp.stop();
            static_cast<void>(co_await detail::settle(executor, [&]() { return serving_ended.load(); }));
            executor.stop();
        };
        executor.spawn(run());
        executor.run();

        // No downgrade (P1): the CONNECT_REQUEST is refused, and no channel was ever allocated for it.
        CHECK(!observed.connected);
        CHECK(observed.refusal);
        CHECK(!channels_after);
        CHECK(server.secure_counters().unencrypted_refused == 1u);
        CHECK(server.secure_counters().sessions_opened == 0u);
    }

#if defined(KMX_AIO_FEATURE_READINESS)
    TEST_CASE("knx readiness secure tcp server tunnels an authenticated client end to end", "[knx][secure][server][integration][readiness]")
    {
        auto executor = std::make_shared<readiness::executor>(readiness::executor_config {.thread_count = 2u, .timeout_ms = 20u});
        kn::generic_server server {detail::secure_config()};
        detail::event_log log {};
        readiness::knx::tcp_server tcp {
            *executor, server, {.bind_address = {127u, 0u, 0u, 1u}, .port = 0u, .on_event = detail::recorder(log)}};
        REQUIRE(tcp.listen().has_value());
        const auto address = detail::loopback(tcp.port());
        readiness::knx::tcp_transport transport {*executor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)};
        detail::tunnel_observation observed {};
        std::atomic_bool serving_ended {};
        std::atomic_size_t finished {};

        executor->spawn(detail::run_server(tcp, serving_ended));
        executor->spawn(detail::counted(detail::run_secure_tunnel(transport, address, server,
                                                                  detail::credentials(detail::first_user, detail::first_password()),
                                                                  detail::request(), observed),
                                        finished));
        std::jthread runner([executor]() { executor->run(); });
        CHECK(detail::wait_until([&]() { return finished.load() == 1u; }));
        CHECK(detail::wait_until(
            [&]() { return (server.active_channels() == 0u) && (server.secure_sessions() == 0u) && (tcp.connections() == 0u); }));
        tcp.stop();
        CHECK(detail::wait_until([&]() { return serving_ended.load(); }));
        executor->stop();
        runner.join();

        detail::check_tunnel(observed, log);
        detail::check_session_closed(server);
    }

    TEST_CASE("knx readiness secure tcp server forgets a handshake whose connection goes", "[knx][secure][server][integration][readiness]")
    {
        auto executor = std::make_shared<readiness::executor>(readiness::executor_config {.thread_count = 2u, .timeout_ms = 20u});
        kn::generic_server server {detail::secure_config()};
        readiness::knx::tcp_server tcp {*executor, server, {.bind_address = {127u, 0u, 0u, 1u}, .port = 0u}};
        REQUIRE(tcp.listen().has_value());
        std::atomic_bool serving_ended {};
        executor->spawn(detail::run_server(tcp, serving_ended));
        std::jthread runner([executor]() { executor->run(); });

        std::vector<std::uint8_t> response {};
        bool held {};
        {
            const auto connection = detail::connect_blocking(tcp.port());
            REQUIRE(connection.is_valid());
            REQUIRE(detail::write_all(connection, detail::session_request()));
            response = detail::read_exactly(connection, ks::session_response_size);
            held = detail::wait_until([&]() { return server.secure_sessions() == 1u; });
        }
        // The connection closed with the handshake half done: the session goes with it, long before its lifetime would end it.
        CHECK(detail::wait_until([&]() { return (server.secure_sessions() == 0u) && (tcp.connections() == 0u); }));
        tcp.stop();
        CHECK(detail::wait_until([&]() { return serving_ended.load(); }));
        executor->stop();
        runner.join();

        CHECK(response.size() == ks::session_response_size);
        CHECK(held);
        CHECK(server.secure_counters().sessions_timed_out == 0u);
    }

    TEST_CASE("knx readiness secure tcp server turns away a third handshake from one address",
              "[knx][secure][server][integration][readiness]")
    {
        auto executor = std::make_shared<readiness::executor>(readiness::executor_config {.thread_count = 2u, .timeout_ms = 20u});
        kn::generic_server server {detail::secure_config()};
        readiness::knx::tcp_server tcp {*executor, server, {.bind_address = {127u, 0u, 0u, 1u}, .port = 0u}};
        REQUIRE(tcp.listen().has_value());
        std::atomic_bool serving_ended {};
        executor->spawn(detail::run_server(tcp, serving_ended));
        std::jthread runner([executor]() { executor->run(); });

        std::array<std::vector<std::uint8_t>, 3u> answers {};
        {
            std::array<file_descriptor, 3u> connections {};
            for (std::size_t index {}; index < connections.size(); ++index)
            {
                connections[index] = detail::connect_blocking(tcp.port());
                REQUIRE(detail::write_all(connections[index], detail::session_request()));
                answers[index] =
                    detail::read_exactly(connections[index], (index < 2u) ? ks::session_response_size : ks::session_status_size);
            }
        }
        CHECK(detail::wait_until([&]() { return (server.secure_sessions() == 0u) && (tcp.connections() == 0u); }));
        tcp.stop();
        CHECK(detail::wait_until([&]() { return serving_ended.load(); }));
        executor->stop();
        runner.join();

        CHECK(answers[0u].size() == ks::session_response_size);
        CHECK(answers[1u].size() == ks::session_response_size);
        // The third is told in the clear, at once, that no session was opened for it: SESSION_STATUS unauthenticated.
        const std::vector<std::uint8_t> refusal {0x06u, 0x10u, 0x09u, 0x54u, 0x00u, 0x08u, 0x02u, 0x00u};
        CHECK(answers[2u] == refusal);
    }

    TEST_CASE("knx readiness secure tcp server closes a connection that never opens a session",
              "[knx][secure][server][integration][readiness]")
    {
        auto executor = std::make_shared<readiness::executor>(readiness::executor_config {.thread_count = 2u, .timeout_ms = 20u});
        kn::generic_server server {detail::secure_config(200u)};
        readiness::knx::tcp_server tcp {*executor, server, {.bind_address = {127u, 0u, 0u, 1u}, .port = 0u}};
        REQUIRE(tcp.listen().has_value());
        std::atomic_bool serving_ended {};
        executor->spawn(detail::run_server(tcp, serving_ended));
        std::jthread runner([executor]() { executor->run(); });

        const auto connection = detail::connect_blocking(tcp.port());
        REQUIRE(connection.is_valid());
        CHECK(detail::wait_until([&]() { return tcp.connections() == 1u; }));
        // Nothing is sent. The server gives the connection as long as a handshake may take, then closes it.
        CHECK(detail::wait_until([&]() { return tcp.connections() == 0u; }));
        CHECK(detail::read_exactly(connection, 1u).empty());
        tcp.stop();
        CHECK(detail::wait_until([&]() { return serving_ended.load(); }));
        executor->stop();
        runner.join();
    }

    TEST_CASE("knx readiness secure tcp server serves several secure clients at once", "[knx][secure][server][integration][readiness]")
    {
        constexpr std::size_t clients = 4u;
        constexpr std::size_t frames = 3u;
        auto executor = std::make_shared<readiness::executor>(readiness::executor_config {.thread_count = 2u, .timeout_ms = 20u});
        // All four handshakes come from 127.0.0.1 at once, past the default share of one address.
        kn::generic_server server {detail::secure_config(10'000u, clients)};
        std::atomic_size_t delivered {};
        auto count_delivered = [&delivered](kn::server_event) -> task<void>
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

        executor->spawn(detail::run_server(tcp, serving_ended));
        for (std::size_t index {}; index < clients; ++index)
        {
            transports.push_back(
                std::make_unique<readiness::knx::tcp_transport>(*executor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)));
            executor->spawn(detail::counted(detail::run_secure_burst(*transports.back(), address, frames, completed), finished));
        }
        std::jthread runner([executor]() { executor->run(); });
        CHECK(detail::wait_until([&]() { return finished.load() == clients; }));
        CHECK(detail::wait_until(
            [&]() { return (server.active_channels() == 0u) && (server.secure_sessions() == 0u) && (tcp.connections() == 0u); }));
        tcp.stop();
        CHECK(detail::wait_until([&]() { return serving_ended.load(); }));
        executor->stop();
        runner.join();

        CHECK(completed.load() == clients);
        CHECK(delivered.load() == clients * frames);
        CHECK(server.secure_counters().sessions_opened == clients);
    }

    TEST_CASE("knx readiness secure tcp server is torn down while a session is open", "[knx][secure][server][integration][readiness]")
    {
        auto executor = std::make_shared<readiness::executor>(readiness::executor_config {.thread_count = 2u, .timeout_ms = 20u});
        std::jthread runner([executor]() { executor->run(); });
        std::unique_ptr<readiness::knx::tcp_transport> transport {};
        std::unique_ptr<kn::tunnelling_client> client {};
        std::atomic_bool connected {};
        std::atomic_bool receive_failed {};
        {
            kn::generic_server server {detail::secure_config()};
            readiness::knx::tcp_server tcp {*executor, server, {.bind_address = {127u, 0u, 0u, 1u}, .port = 0u}};
            REQUIRE(tcp.listen().has_value());
            const auto address = detail::loopback(tcp.port());
            transport =
                std::make_unique<readiness::knx::tcp_transport>(*executor, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
            client = std::make_unique<kn::tunnelling_client>(*transport, reinterpret_cast<const sockaddr*>(&address), sizeof(address),
                                                             kn::tunnelling_config {},
                                                             detail::credentials(detail::first_user, detail::first_password()));
            std::atomic_bool serving_ended {};
            executor->spawn(detail::run_server(tcp, serving_ended));
            executor->spawn([](kn::tunnelling_client& value, std::atomic_bool& done) -> task<void>
                            { done = (co_await value.connect(detail::request())).has_value(); }(*client, connected));
            CHECK(detail::wait_until([&]() { return connected.load() && (server.secure_sessions() == 1u); }));
            tcp.stop();
            CHECK(detail::wait_until([&]() { return serving_ended.load() && (tcp.connections() == 0u); }));
        }
        // The server and its accept loop are gone. The client's next receive meets the end of its connection, and nothing of
        // the server resumes. A heartbeat would not do: over TCP it is only sent.
        std::atomic_size_t finished {};
        executor->spawn(detail::counted([](kn::tunnelling_client& value, std::atomic_bool& failed) -> task<void>
                                        { failed = !(co_await value.receive_cemi()).has_value(); }(*client, receive_failed), finished));
        CHECK(detail::wait_until([&]() { return finished.load() == 1u; }));
        executor->stop();
        runner.join();
        CHECK(receive_failed.load());
    }

    TEST_CASE("knx readiness secure tcp server keeps a session on a quiet bus open through keep-alives",
              "[knx][secure][server][integration][readiness]")
    {
        auto executor = std::make_shared<readiness::executor>(readiness::executor_config {.thread_count = 2u, .timeout_ms = 20u});
        detail::session_clock_ms = 0u;
        kn::generic_server server {detail::secure_config(), nullptr, detail::session_clock};
        std::atomic_size_t delivered {};
        auto count_delivered = [&delivered](kn::server_event) -> task<void>
        {
            delivered.fetch_add(1u);
            co_return;
        };
        readiness::knx::tcp_server tcp {*executor, server, {.bind_address = {127u, 0u, 0u, 1u}, .port = 0u, .on_event = count_delivered}};
        REQUIRE(tcp.listen().has_value());
        const auto address = detail::loopback(tcp.port());
        readiness::knx::tcp_transport transport {*executor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)};
        kn::tunnelling_client client {transport,
                                      reinterpret_cast<const sockaddr*>(&address),
                                      sizeof(address),
                                      kn::tunnelling_config {},
                                      detail::credentials(detail::first_user, detail::first_password()),
                                      nullptr,
                                      detail::session_clock};
        detail::quiet_observation observed {};
        std::atomic_bool serving_ended {};
        std::atomic_size_t finished {};

        executor->spawn(detail::run_server(tcp, serving_ended));
        executor->spawn(detail::counted(detail::keep_quiet_session(*executor, client, server, observed), finished));
        std::jthread runner([executor]() { executor->run(); });
        CHECK(detail::wait_until([&]() { return finished.load() == 1u; }));
        CHECK(detail::wait_until([&]()
                                 { return (delivered.load() == 1u) && (server.secure_sessions() == 0u) && (tcp.connections() == 0u); }));
        tcp.stop();
        CHECK(detail::wait_until([&]() { return serving_ended.load(); }));
        executor->stop();
        runner.join();

        // Five minutes of session time, far past the 60 s timeout, and the keep-alives alone kept both ends open.
        CHECK(observed.connected);
        CHECK(observed.kept_alive == 6u);
        CHECK(observed.open_after);
        CHECK(observed.sent);
        CHECK(observed.disconnected);
        CHECK(server.secure_counters().sessions_timed_out == 0u);
    }
#endif
}
