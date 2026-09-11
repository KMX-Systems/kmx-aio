/// @file src/kmx/aio/knx/secure/tunnelling_secure_interop_test.cpp
/// @brief KNX IP Secure tunnelling against external peers: calimero-server for the in-tree client, xknx for the in-tree server.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details script/feature/knx/interop/run-secure-tunnelling-interop.sh starts the peer and passes its port in
/// KMX_KNX_INTEROP_PORT; without it each case is skipped. The client opens a session with the user and passwords the
/// server was configured with, then tunnels one switch-on to 1/2/3, waits for the server's confirmation, and sends a
/// heartbeat and a keep-alive before closing both the tunnel and the session. The server is keyed from the keyring in
/// KMX_KNX_INTEROP_KEYRING, answers description over UDP on its port as well - xknx reads it before a secure tunnel whose
/// credentials come from a keyring - and confirms the external client's switch-on to 1/2/3, then answers it on 1/2/4.
#ifndef PCH
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/completion/knx/tcp_server.hpp>
    #include <kmx/aio/completion/knx/tcp_transport.hpp>
    #include <kmx/aio/completion/knx/udp_transport.hpp>
    #include <kmx/aio/completion/timer.hpp>
    #include <kmx/aio/completion/udp/endpoint.hpp>
    #include <kmx/aio/knx/cemi.hpp>
    #include <kmx/aio/knx/cemi_frame.hpp>
    #include <kmx/aio/knx/dpt.hpp>
    #include <kmx/aio/knx/generic_server.hpp>
    #include <kmx/aio/knx/keyring.hpp>
    #include <kmx/aio/knx/keyring/document.hpp>
    #include <kmx/aio/knx/secure/key.hpp>
    #include <kmx/aio/knx/server.hpp>
    #include <kmx/aio/knx/telegram.hpp>
    #include <kmx/aio/knx/tunnelling_client.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <algorithm>
    #include <array>
    #include <atomic>
    #include <chrono>
    #include <condition_variable>
    #include <cstdint>
    #include <cstdlib>
    #include <fstream>
    #include <iterator>
    #include <memory>
    #include <mutex>
    #include <stop_token>
    #include <string>
    #include <string_view>
    #include <thread>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace kmx::aio::test::knx::secure::tunnelling_secure_interop_test
{
    namespace kn = kmx::aio::knx;
    namespace ks = kmx::aio::knx::secure;

    namespace detail
    {
        /// @brief The group the switch-on goes to: 1/2/3.
        constexpr std::uint16_t request_group = 0x0A03u;
        /// @brief The group the in-tree server answers on: 1/2/4.
        constexpr std::uint16_t answer_group = 0x0A04u;
        /// @brief This client's serial number: `kmx` and a two.
        constexpr ks::serial_number_t serial {0x00u, 0x00u, 0x6Bu, 0x6Du, 0x78u, 0x02u};
        /// @brief The in-tree server's serial number: `kmx` and a three.
        constexpr ks::serial_number_t server_serial {0x00u, 0x00u, 0x6Bu, 0x6Du, 0x78u, 0x03u};
        /// @brief The keyring device whose tunnels the in-tree server serves.
        constexpr kn::individual_address host {1u, 0u, 0u};

        /// @brief What the client saw of its session and tunnel.
        struct outcome
        {
            std::error_code connect_error {};
            bool connected {};
            bool sent {};
            std::size_t frames {};
            bool confirmed {};
            bool beat {};
            bool kept_alive {};
            bool disconnected {};
        };

        /// @brief What the in-tree server saw of the external client's session and tunnel.
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

        /// @brief Builds the credentials the server was configured with, from the environment or its defaults.
        [[nodiscard]] ks::tunnelling_credentials credentials() noexcept(false)
        {
            auto user_key = ks::derive_user_password_key(environment("KMX_KNX_INTEROP_USER_PASSWORD", "secret"));
            auto device_code = ks::derive_device_authentication_code(environment("KMX_KNX_INTEROP_DEVICE_PASSWORD", "trustme"));
            REQUIRE(user_key.has_value());
            REQUIRE(device_code.has_value());
            return ks::tunnelling_credentials {.user_id =
                                                   static_cast<std::uint8_t>(std::stoul(environment("KMX_KNX_INTEROP_USER_ID", "2"))),
                                               .user_password_key = std::move(*user_key),
                                               .device_authentication_code = std::move(*device_code),
                                               .serial_number = serial};
        }

        /// @brief Opens the session and tunnel, exchanges a switch-on and its confirmation, and closes both.
        task<void> tunnel_to_peer(completion::executor& executor, kn::tunnelling_client& client, const kn::dpt::payload& switch_on,
                                  outcome& observed)
        {
            const auto connected = co_await client.connect(kn::connect_request_frame {});
            observed.connect_error = connected.has_value() ? std::error_code {} : connected.error();
            observed.connected = connected.has_value();
            if (observed.connected)
            {
                observed.sent = (co_await client.write_group_value(kn::group_address {request_group}, switch_on)).has_value();
                // The server confirms each request it passes on to its bus, inside the session.
                for (std::size_t attempt {}; observed.sent && !observed.confirmed && (attempt < 8u); ++attempt)
                {
                    const auto telegram = co_await client.receive_telegram();
                    if (!telegram.has_value())
                        break;
                    ++observed.frames;
                    observed.confirmed = (telegram->frame.message_code == kn::cemi_message_code::l_data_con) &&
                                         (telegram->frame.destination == request_group);
                }

                observed.beat = (co_await client.heartbeat()).has_value();
                observed.kept_alive = (co_await client.keep_alive()).has_value();
                observed.disconnected = (co_await client.disconnect()).has_value();
            }

            executor.stop();
        }

        /// @brief Loads the keyring in KMX_KNX_INTEROP_KEYRING and builds the server configuration for its tunnels on @ref host.
        [[nodiscard]] std::shared_ptr<const ks::server_configuration> keyed_configuration() noexcept(false)
        {
            std::ifstream file {environment("KMX_KNX_INTEROP_KEYRING", "")};
            const std::string document {std::istreambuf_iterator<char> {file}, std::istreambuf_iterator<char> {}};
            const auto keyring = kn::keyring::load(document, environment("KMX_KNX_INTEROP_KEYRING_PASSWORD", "password"));
            REQUIRE(keyring.has_value());
            auto configuration = kn::keyring::server_configuration_for(*keyring, host, server_serial);
            REQUIRE(configuration.has_value());
            return std::make_shared<const ks::server_configuration>(std::move(*configuration));
        }

        /// @brief The secure server's configuration, with the description a client reads before it connects.
        [[nodiscard]] kn::server_config server_settings(const port_t port) noexcept(false)
        {
            kn::dib::device_info device {.address = host, .serial_number = server_serial};
            constexpr std::string_view name {"kmx-aio secure interop"};
            std::ranges::copy(name, device.friendly_name.begin());
            const kn::dib::supported_service_families families {.families = {{kn::dib::service_family::core, 2u},
                                                                             {kn::dib::service_family::tunnelling, 2u},
                                                                             {kn::dib::service_family::security, 1u}}};
            return kn::server_config {.max_channels = 4u,
                                      .control_endpoint = kn::hpai {kn::ipv4_endpoint {{127u, 0u, 0u, 1u}, port}, 0x01u},
                                      .description_blocks = {device, families},
                                      .secure = keyed_configuration()};
        }

        /// @brief Indicates whether cEMI octets are an L_Data.req to group @p group.
        [[nodiscard]] bool request_to(const cspan_uint8_t octets, const std::uint16_t group) noexcept
        {
            if ((octets.size() < 2u) || (octets[0u] != static_cast<std::uint8_t>(kn::cemi_message_code::l_data_req)))
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
            const auto value = kn::dpt::encode<1u>(true);
            REQUIRE(value.has_value());
            std::array<std::uint8_t, kn::cemi::max_l_data_size> message {};
            const auto size = kn::cemi::encode(message, {.code = kn::cemi_message_code::l_data_ind,
                                                         .source = kn::individual_address {1u, 1u, 1u},
                                                         .destination = kn::group_address {answer_group}.value(),
                                                         .service = kn::apci::group_value_write,
                                                         .payload = value->apdu()});
            REQUIRE(size.has_value());
            return byte_buffer_t(message.begin(), message.begin() + static_cast<std::ptrdiff_t>(*size));
        }

        /// @brief Confirms the external client's switch-on request, as a server passing it to a bus would, then answers it.
        [[nodiscard]] kn::server_event_handler answer_peer(kn::generic_server& server, const byte_buffer_t& answer,
                                                           server_outcome& observed)
        {
            return [&server, &answer, &observed](kn::server_event event) -> task<void>
            {
                if (!request_to(event.cemi_bytes, request_group))
                    co_return;
                observed.requested = true;
                // xknx sends nothing further until its request is confirmed.
                auto confirmation = event.cemi_bytes;
                confirmation[0u] = static_cast<std::uint8_t>(kn::cemi_message_code::l_data_con);
                observed.confirmed = (co_await server.send(event.channel_id, confirmation)).has_value();
                observed.answered = (co_await server.send(event.channel_id, answer)).has_value();
            };
        }

        /// @brief Serves datagrams - the description requests - until the server shuts down, then records that it ended.
        task<void> run_datagrams(kn::generic_server& server, std::atomic_bool& ended)
        {
            static_cast<void>(co_await server.serve());
            ended = true;
        }

        /// @brief Runs the accept loop, then records that it ended.
        task<void> run_connections(completion::knx::tcp_server& tcp, std::atomic_bool& ended)
        {
            static_cast<void>(co_await tcp.serve());
            ended = true;
        }

        /// @brief Sends one empty datagram to the server's UDP port, so the receive its datagram loop waits in returns.
        void wake(const port_t port) noexcept
        {
            const auto address = loopback(port);
            const auto descriptor = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
            if (descriptor < 0)
                return;
            static_cast<void>(::sendto(descriptor, "", 0u, 0, reinterpret_cast<const sockaddr*>(&address), sizeof(address)));
            static_cast<void>(::close(descriptor));
        }

        /// @brief The loop, the server and its accept loop an exchange with the external peer runs on.
        struct server_side
        {
            /// @brief The loop, which the exchange stops once it is over.
            completion::executor& executor;
            /// @brief The server the peer tunnels through.
            kn::generic_server& server;
            /// @brief The accept loop serving the server.
            completion::knx::tcp_server& tcp;
            /// @brief The server's UDP port, which the datagram loop waits on.
            port_t port {};
        };

        /// @brief Waits for the exchange to finish and the client to close its tunnel and session, then stops everything.
        task<void> await_peer(const server_side side, server_outcome& observed, const std::atomic_bool& connections_ended,
                              const std::atomic_bool& datagrams_ended)
        {
            completion::timer pause {side.executor};
            while (!observed.answered || (side.server.active_channels() != 0u) || (side.server.secure_sessions() != 0u) ||
                   (side.tcp.connections() != 0u))
                static_cast<void>(co_await pause.wait(std::chrono::milliseconds {20}));
            observed.released = true;
            side.tcp.stop();
            static_cast<void>(side.server.shutdown());
            wake(side.port);
            while (!connections_ended || !datagrams_ended)
                static_cast<void>(co_await pause.wait(std::chrono::milliseconds {5}));
            side.executor.stop();
        }
    }

    TEST_CASE("knx secure tunnelling client tunnels through an external KNX IP Secure server", "[knx][secure][tunnelling][interop]")
    {
        const auto port = detail::environment("KMX_KNX_INTEROP_PORT", "");
        if (port.empty())
            SKIP("KMX_KNX_INTEROP_PORT is unset: script/feature/knx/interop/run-secure-tunnelling-interop.sh starts a server and sets it");

        const auto switch_on = kn::dpt::encode<1u>(true);
        REQUIRE(switch_on.has_value());
        completion::executor executor;
        const auto address = detail::loopback(static_cast<port_t>(std::stoul(port)));
        completion::knx::tcp_transport transport {executor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)};
        kn::tunnelling_client client {
            transport, reinterpret_cast<const sockaddr*>(&address), sizeof(address), {.credentials = detail::credentials()}};
        detail::outcome observed {};
        executor.spawn(detail::tunnel_to_peer(executor, client, *switch_on, observed));
        detail::run_bounded(executor, detail::time_limit());

        const auto counters = client.secure_counters();
        INFO("connect=" << observed.connect_error.message() << " channel=" << static_cast<int>(client.channel_id())
                        << " frames=" << observed.frames << " opened=" << counters.sessions_opened << " closed=" << counters.sessions_closed
                        << " authentication_failures=" << counters.authentication_failures << " replays=" << counters.replays
                        << " unencrypted_refused=" << counters.unencrypted_refused);
        CHECK(observed.connected);
        CHECK(observed.sent);
        CHECK(observed.confirmed);
        CHECK(observed.beat);
        CHECK(observed.kept_alive);
        CHECK(observed.disconnected);
        CHECK(counters.sessions_opened == 1u);
        CHECK(counters.authentication_failures == 0u);
        CHECK(counters.replays == 0u);
    }

    TEST_CASE("knx secure tunnelling server serves an external KNX IP Secure client", "[knx][secure][server][interop]")
    {
        const auto port_text = detail::environment("KMX_KNX_INTEROP_PORT", "");
        if (port_text.empty())
            SKIP("KMX_KNX_INTEROP_PORT is unset: script/feature/knx/interop/run-secure-tunnelling-interop.sh starts a client and sets it");

        const auto port = static_cast<port_t>(std::stoul(port_text));
        const auto answer = detail::answer_indication();
        completion::executor executor;
        static constexpr ipv4::storage_t loopback_address {127u, 0u, 0u, 1u};
        auto endpoint = completion::udp::endpoint::create(executor, AF_INET);
        REQUIRE(endpoint.has_value());
        REQUIRE(endpoint->raw().bind(ipv4::make_address(loopback_address), port).has_value());
        completion::knx::udp_transport datagrams {*endpoint};
        kn::generic_server server {datagrams, detail::server_settings(port)};
        detail::server_outcome observed {};
        completion::knx::tcp_server tcp {
            executor,
            server,
            {.bind_address = {127u, 0u, 0u, 1u}, .port = port, .on_event = detail::answer_peer(server, answer, observed)}};
        REQUIRE(tcp.listen().has_value());
        std::atomic_bool connections_ended {};
        std::atomic_bool datagrams_ended {};
        executor.spawn(detail::run_datagrams(server, datagrams_ended));
        executor.spawn(detail::run_connections(tcp, connections_ended));
        executor.spawn(detail::await_peer({executor, server, tcp, port}, observed, connections_ended, datagrams_ended));
        detail::run_bounded(executor, detail::time_limit());

        const auto counters = server.secure_counters();
        INFO("channels=" << static_cast<int>(server.active_channels()) << " connections=" << tcp.connections()
                         << " sessions=" << server.secure_sessions() << " opened=" << counters.sessions_opened
                         << " closed=" << counters.sessions_closed << " authentication_failures=" << counters.authentication_failures
                         << " replays=" << counters.replays << " unencrypted_refused=" << counters.unencrypted_refused);
        CHECK(observed.requested);
        CHECK(observed.confirmed);
        CHECK(observed.answered);
        CHECK(observed.released);
        CHECK(counters.sessions_opened == 1u);
        CHECK(counters.authentication_failures == 0u);
        CHECK(counters.replays == 0u);
        CHECK(counters.unencrypted_refused == 0u);
    }
}
