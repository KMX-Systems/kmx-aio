/// @file src/kmx/aio/knx/integration_test.cpp
/// @brief KNX UDP transports on real loopback sockets, on both models: tunnelling lifecycle, deadlines and IPv6 SEARCH.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#ifndef PCH
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/completion/knx/udp_transport.hpp>
    #include <kmx/aio/completion/udp/endpoint.hpp>
    #include <kmx/aio/file_descriptor.hpp>
    #include <kmx/aio/knx/connection.hpp>
    #include <kmx/aio/knx/discovery/client.hpp>
    #include <kmx/aio/knx/frame.hpp>
    #include <kmx/aio/knx/tunnelling_client.hpp>
    #include <kmx/aio/test/knx/telegram.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <array>
    #include <chrono>
    #include <cstdint>
    #include <thread>
    #include <vector>

    #if defined(KMX_AIO_FEATURE_READINESS)
        #include <kmx/aio/readiness/executor.hpp>
        #include <kmx/aio/readiness/knx/udp_transport.hpp>
        #include <kmx/aio/readiness/udp/endpoint.hpp>
    #endif
#endif

namespace kmx::aio::test::knx::integration_test
{
    using namespace kmx::aio::knx;

    namespace detail
    {
        struct udp_socket_binding
        {
            file_descriptor socket {};
            sockaddr_storage address {};
            ::socklen_t length = 0u;
            port_t port {};
        };

        struct server_observation
        {
            bool connect_received {};
            bool tunnelling_received {};
            bool disconnect_received {};
        };

        [[nodiscard]] udp_socket_binding bind_loopback_udp_socket()
        {
            auto socket = file_descriptor::create_socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
            REQUIRE(socket.has_value());

            sockaddr_in address {};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons(0u);
            REQUIRE(socket->bind(reinterpret_cast<const sockaddr*>(&address), sizeof(address)).has_value());

            udp_socket_binding binding {.socket = std::move(*socket), .length = sizeof(sockaddr_in)};
            binding.length = sizeof(binding.address);
            REQUIRE(::getsockname(binding.socket.get(), reinterpret_cast<sockaddr*>(&binding.address), &binding.length) == 0);
            binding.port = ntohs(reinterpret_cast<const sockaddr_in&>(binding.address).sin_port);

            timeval timeout {.tv_sec = 2, .tv_usec = 0};
            REQUIRE(binding.socket.setsockopt(SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)).has_value());
            REQUIRE(binding.socket.setsockopt(SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)).has_value());
            return binding;
        }

        [[nodiscard]] udp_socket_binding bind_loopback_udp6_socket()
        {
            auto socket = file_descriptor::create_socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0);
            REQUIRE(socket.has_value());
            int v6_only = 1;
            REQUIRE(socket->setsockopt(IPPROTO_IPV6, IPV6_V6ONLY, &v6_only, sizeof(v6_only)).has_value());
            sockaddr_in6 address {};
            address.sin6_family = AF_INET6;
            address.sin6_addr = in6addr_loopback;
            address.sin6_port = htons(0u);
            REQUIRE(socket->bind(reinterpret_cast<const sockaddr*>(&address), sizeof(address)).has_value());

            udp_socket_binding binding {.socket = std::move(*socket), .length = sizeof(sockaddr_in6)};
            binding.length = sizeof(binding.address);
            REQUIRE(::getsockname(binding.socket.get(), reinterpret_cast<sockaddr*>(&binding.address), &binding.length) == 0);
            binding.port = ntohs(reinterpret_cast<const sockaddr_in6&>(binding.address).sin6_port);
            return binding;
        }

        [[nodiscard]] connect_request_frame make_loopback_request(const port_t client_port)
        {
            return connect_request_frame {
                .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, client_port}, 0x01u},
                .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, client_port}, 0x01u},
            };
        }

        /// @brief Receives one datagram, remembering who sent it.
        /// @param fd The bound socket to receive on.
        /// @param buffer Where to receive into.
        /// @param peer Receives the sender's address.
        /// @param peer_length Receives how many octets of @p peer are meaningful.
        /// @return The datagram's octets, empty when nothing arrived.
        [[nodiscard]] cspan_uint8_t receive_from(const fd_t fd, std::span<std::uint8_t> buffer, sockaddr_storage& peer,
                                                 ::socklen_t& peer_length)
        {
            peer = {};
            peer_length = sizeof(peer);
            const auto received = ::recvfrom(fd, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&peer), &peer_length);
            if (received <= 0)
                return {};
            return {buffer.data(), static_cast<std::size_t>(received)};
        }

        /// @brief Sends one datagram back to the peer a request came from.
        /// @return `true` when the datagram was handed to the kernel.
        [[nodiscard]] bool send_to(const fd_t fd, const cspan_uint8_t packet, const sockaddr_storage& peer, const ::socklen_t peer_length)
        {
            return ::sendto(fd, packet.data(), packet.size(), 0, reinterpret_cast<const sockaddr*>(&peer), peer_length) >= 0;
        }

        /// @brief Answers one CONNECT_REQUEST on the control socket, pointing the client at the data one.
        [[nodiscard]] bool serve_connect_request(const udp_socket_binding& control, const udp_socket_binding& data,
                                                 std::span<std::uint8_t> buffer, server_observation& out)
        {
            sockaddr_storage peer {};
            ::socklen_t peer_length {};
            const auto packet = receive_from(control.socket.get(), buffer, peer, peer_length);
            if (packet.empty() || !connection::decode_connect_request_packet(packet).has_value())
                return false;

            out.connect_received = true;
            std::vector<std::uint8_t> response(20u, 0u);
            const connect_response_frame value {
                .channel_id = 3u,
                .status = connect_status::no_error,
                .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, data.port}, 0x01u},
                .assigned_address = individual_address {1u, 1u, 10u},
            };
            if (!connection::encode_connect_response_packet(response, value).has_value())
                return false;
            return send_to(control.socket.get(), response, peer, peer_length);
        }

        /// @brief Acknowledges one TUNNELLING_REQUEST on the data socket.
        [[nodiscard]] bool serve_tunnelling_request(const udp_socket_binding& data, std::span<std::uint8_t> buffer, server_observation& out)
        {
            sockaddr_storage peer {};
            ::socklen_t peer_length {};
            const auto packet = receive_from(data.socket.get(), buffer, peer, peer_length);
            if (packet.empty())
                return false;

            const auto request = frame::decode_tunnelling_request_packet(packet);
            if (!request.has_value())
                return false;

            out.tunnelling_received = true;
            std::vector<std::uint8_t> ack(10u, 0u);
            if (!frame::encode_tunnelling_ack_packet(ack, request->channel_id, request->sequence_number).has_value())
                return false;
            return send_to(data.socket.get(), ack, peer, peer_length);
        }

        /// @brief Answers one DISCONNECT_REQUEST on the control socket.
        [[nodiscard]] bool serve_disconnect_request(const udp_socket_binding& control, std::span<std::uint8_t> buffer,
                                                    server_observation& out)
        {
            sockaddr_storage peer {};
            ::socklen_t peer_length {};
            const auto packet = receive_from(control.socket.get(), buffer, peer, peer_length);
            if (packet.empty())
                return false;

            const auto request = connection::decode_disconnect_request_packet(packet);
            if (!request.has_value())
                return false;

            out.disconnect_received = true;
            std::vector<std::uint8_t> response(8u, 0u);
            const disconnect_response_frame value {request->channel_id, connect_status::no_error};
            if (!connection::encode_disconnect_response_packet(response, value).has_value())
                return false;
            return send_to(control.socket.get(), response, peer, peer_length);
        }

        /// @brief Plays a whole tunnelling lifecycle back at a client: connect, one frame, disconnect.
        /// @details Each step stops the run when it fails, so a client that never got past connect leaves
        ///          the later observations unset rather than blocking on a datagram that will not come.
        void run_loopback_server(const udp_socket_binding& control_binding, const udp_socket_binding& data_binding,
                                 server_observation& out) noexcept
        {
            std::array<std::uint8_t, frame::max_datagram_size> buffer {};
            if (!serve_connect_request(control_binding, data_binding, buffer, out))
                return;
            if (!serve_tunnelling_request(data_binding, buffer, out))
                return;

            static_cast<void>(serve_disconnect_request(control_binding, buffer, out));
        }

        [[nodiscard]] port_t bound_port(const fd_t fd)
        {
            sockaddr_storage address {};
            ::socklen_t length = sizeof(address);
            REQUIRE(::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) == 0);
            REQUIRE(length >= sizeof(sockaddr_in));
            return ntohs(reinterpret_cast<const sockaddr_in&>(address).sin_port);
        }

        /// @brief Connects, sends one cEMI and disconnects over the completion transport.
        task<void> run_completion_lifecycle(tunnelling_client& client, bool& succeeded, completion::executor& executor,
                                            const int fd) noexcept(false)
        {
            const auto request = detail::make_loopback_request(detail::bound_port(fd));
            const auto connected = co_await client.connect(request);
            const auto sent = connected ? co_await client.send(sample_cemi) : expected_void_t {std::unexpected(connected.error())};
            const auto disconnected = sent ? co_await client.disconnect() : expected_void_t {std::unexpected(sent.error())};
            succeeded = connected.has_value() && sent.has_value() && disconnected.has_value();
            executor.stop();
        }

        /// @brief Issues one IPv6 SEARCH and records whether the expected response came back.
        task<void> run_ipv6_search(discovery::client& client, const udp_socket_binding& server_binding, bool& succeeded,
                                   completion::executor& executor) noexcept(false)
        {
            const auto result = co_await client.search(discovery::ipv6_search_request_frame {
                .discovery_endpoint =
                    ipv6_hpai {
                        ipv6_endpoint {{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u}, server_binding.port},
                        0x01u,
                    },
            });
            succeeded = result.has_value() && std::holds_alternative<discovery::ipv6_search_response_frame>(*result);
            executor.stop();
        }

#if defined(KMX_AIO_FEATURE_READINESS)
        /// @brief Connects, sends one cEMI and disconnects over the readiness transport.
        task<void> run_readiness_lifecycle(const std::shared_ptr<readiness::executor>& executor, tunnelling_client& client, const int fd,
                                           bool& succeeded) noexcept(false)
        {
            const auto request = detail::make_loopback_request(detail::bound_port(fd));
            const auto connected = co_await client.connect(request);
            const auto sent = connected ? co_await client.send(sample_cemi) : expected_void_t {std::unexpected(connected.error())};
            const auto disconnected = sent ? co_await client.disconnect() : expected_void_t {std::unexpected(sent.error())};
            succeeded = connected.has_value() && sent.has_value() && disconnected.has_value();
            executor->stop();
        }
#endif // KMX_AIO_FEATURE_READINESS

        /// @brief Answers one IPv6 SEARCH request on the loopback socket the test bound.
        /// @param server_binding The bound socket to receive on and reply from.
        /// @param server_received Set once a well-formed request arrived.
        void serve_ipv6_search(const udp_socket_binding& server_binding, bool& server_received)
        {
            std::array<std::uint8_t, frame::max_datagram_size> buffer {};
            sockaddr_storage peer {};
            ::socklen_t peer_length {};
            const auto packet = receive_from(server_binding.socket.get(), buffer, peer, peer_length);
            if (packet.empty() || !discovery::decode_ipv6_search_request_packet(packet).has_value())
                return;
            server_received = true;
            std::array<std::uint8_t, 30u> response {};
            if (!discovery::encode_ipv6_search_response_packet(
                     response,
                     discovery::ipv6_search_response_frame {
                         .control_endpoint =
                             ipv6_hpai {
                                 ipv6_endpoint {{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u}, 3672u},
                                 0x01u,
                             },
                         .device_info_blocks = {0x04u, 0x02u, 0x01u, 0x00u},
                     })
                     .has_value())
                return;
            static_cast<void>(send_to(server_binding.socket.get(), response, peer, peer_length));
        }
    }

    TEST_CASE("knx round-trips a full packet through the codec", "[knx][integration]")
    {
        std::vector<std::uint8_t> packet(sample_tunnelling_packet_size, 0u);
        const auto& cemi = sample_cemi;

        REQUIRE(frame::encode_tunnelling_request_packet(packet, 7u, 2u, cemi).has_value());

        const auto decoded = frame::decode_tunnelling_request_packet(packet);
        REQUIRE(decoded.has_value());
        CHECK(decoded->channel_id == 7u);
        CHECK(decoded->sequence_number == 2u);
        CHECK(decoded->cemi.message_code == cemi_message_code::l_data_req);
    }

    TEST_CASE("knx full packet decoder rejects a truncated declared length", "[knx][integration]")
    {
        const std::array<std::uint8_t, 10u> packet {
            0x06u, 0x10u, 0x04u, 0x20u, 0x00u, 0x12u, 0x04u, 0x07u, 0x02u, 0x00u,
        };

        const auto decoded = frame::decode_tunnelling_request_packet(packet);
        REQUIRE(!decoded.has_value());
        CHECK(decoded.error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("knx completion transport completes a real UDP loopback lifecycle", "[knx][integration][socket][completion]")
    {
        auto control_binding = detail::bind_loopback_udp_socket();
        auto data_binding = detail::bind_loopback_udp_socket();
        detail::server_observation observed {};
        std::jthread server([&]() { detail::run_loopback_server(control_binding, data_binding, observed); });

        completion::executor executor;
        auto endpoint = completion::udp::endpoint::create(executor, AF_INET);
        REQUIRE(endpoint.has_value());
        sockaddr_in local_bind {};
        local_bind.sin_family = AF_INET;
        local_bind.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        local_bind.sin_port = htons(0u);
        REQUIRE(::bind(endpoint->raw().get_fd(), reinterpret_cast<const sockaddr*>(&local_bind), sizeof(local_bind)) == 0);

        completion::knx::udp_transport transport {*endpoint};
        tunnelling_client client {transport, control_binding.address, control_binding.length};
        bool succeeded {};

        executor.spawn(detail::run_completion_lifecycle(client, succeeded, executor, endpoint->raw().get_fd()));
        executor.run();
        if (server.joinable())
            server.join();

        CHECK(succeeded);
        CHECK(observed.connect_received);
        CHECK(observed.tunnelling_received);
        CHECK(observed.disconnect_received);
    }

    TEST_CASE("knx completion transport enforces a real receive deadline", "[knx][integration][socket][completion]")
    {
        auto peer_binding = detail::bind_loopback_udp_socket();
        completion::executor executor;
        auto endpoint = completion::udp::endpoint::create(executor, AF_INET);
        REQUIRE(endpoint.has_value());
        sockaddr_in local_bind {};
        local_bind.sin_family = AF_INET;
        local_bind.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        local_bind.sin_port = htons(0u);
        REQUIRE(::bind(endpoint->raw().get_fd(), reinterpret_cast<const sockaddr*>(&local_bind), sizeof(local_bind)) == 0);

        completion::knx::udp_transport transport {*endpoint};
        tunnelling_client client {
            transport,
            peer_binding.address,
            peer_binding.length,
            {.config = {.max_retries = 0u, .ack_timeout_ms = 50u, .connect_timeout_ms = 50u}},
        };
        bool timed_out {};
        auto run = [&]() -> task<void>
        {
            const auto result = co_await client.connect(detail::make_loopback_request(detail::bound_port(endpoint->raw().get_fd())));
            timed_out = !result.has_value() && result.error() == make_error_code(error::timeout);
            executor.stop();
        };

        executor.spawn(run());
        executor.run();
        CHECK(timed_out);
    }

    TEST_CASE("knx completion discovery client completes a real IPv6 SEARCH", "[knx][integration][socket][completion][ipv6]")
    {
        auto server_binding = detail::bind_loopback_udp6_socket();
        auto executor = completion::executor {};
        auto endpoint = completion::udp::endpoint::create(executor, AF_INET6);
        REQUIRE(endpoint.has_value());
        sockaddr_in6 local_bind {};
        local_bind.sin6_family = AF_INET6;
        local_bind.sin6_addr = in6addr_loopback;
        local_bind.sin6_port = htons(0u);
        REQUIRE(::bind(endpoint->raw().get_fd(), reinterpret_cast<const sockaddr*>(&local_bind), sizeof(local_bind)) == 0);

        sockaddr_storage server_peer = server_binding.address;
        bool server_received {};
        bool succeeded {};
        std::jthread server([&]() { detail::serve_ipv6_search(server_binding, server_received); });

        class endpoint_transport final: public datagram_transport
        {
        public:
            completion::udp::endpoint& endpoint;
            explicit endpoint_transport(completion::udp::endpoint& value) noexcept: endpoint(value) {}
            task_returning_expected_size_t send(const cspan_byte_t payload, const sockaddr* peer,
                                                const socklen_t length) noexcept(false) override
            {
                co_return co_await endpoint.send(payload, peer, length);
            }
            task_returning_expected_size_t receive(const span_byte_t buffer, transport_peer& peer) noexcept(false) override
            {
                co_return co_await endpoint.recv(buffer, peer.address, peer.length);
            }
        } transport {*endpoint};
        discovery::client client {transport, server_peer, server_binding.length};
        executor.spawn(detail::run_ipv6_search(client, server_binding, succeeded, executor));
        executor.run();
        if (server.joinable())
            server.join();
        CHECK(server_received);
        CHECK(succeeded);
    }

#if defined(KMX_AIO_FEATURE_READINESS)
    TEST_CASE("knx readiness transport enforces a real receive deadline", "[knx][integration][socket][readiness]")
    {
        auto executor = std::make_shared<readiness::executor>(readiness::executor_config {.thread_count = 1u, .timeout_ms = 20u});
        auto endpoint = readiness::udp::endpoint::create(*executor, AF_INET);
        REQUIRE(endpoint.has_value());
        sockaddr_in local_bind {};
        local_bind.sin_family = AF_INET;
        local_bind.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        local_bind.sin_port = htons(0u);
        REQUIRE(::bind(endpoint->raw().get_fd(), reinterpret_cast<const sockaddr*>(&local_bind), sizeof(local_bind)) == 0);

        sockaddr_in peer_address {};
        peer_address.sin_family = AF_INET;
        peer_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        peer_address.sin_port = htons(9u);
        readiness::knx::udp_transport transport {*endpoint};
        tunnelling_client client {
            transport,
            reinterpret_cast<const sockaddr*>(&peer_address),
            sizeof(peer_address),
            {.config = {.max_retries = 0u, .ack_timeout_ms = 50u, .connect_timeout_ms = 50u}},
        };
        bool timed_out {};

        auto run = [&]() -> task<void>
        {
            const auto result = co_await client.connect(detail::make_loopback_request(detail::bound_port(endpoint->raw().get_fd())));
            timed_out = !result.has_value() && result.error() == make_error_code(error::timeout);
            executor->stop();
        };

        executor->spawn(run());
        std::jthread runner([executor]() { executor->run(); });
        if (runner.joinable())
            runner.join();
        CHECK(timed_out);
    }

    TEST_CASE("knx readiness transport completes a real UDP loopback lifecycle", "[knx][integration][socket][readiness]")
    {
        auto control_binding = detail::bind_loopback_udp_socket();
        auto data_binding = detail::bind_loopback_udp_socket();
        detail::server_observation observed {};
        std::jthread server([&]() { detail::run_loopback_server(control_binding, data_binding, observed); });

        auto executor = std::make_shared<readiness::executor>(readiness::executor_config {.thread_count = 1u, .timeout_ms = 20u});
        auto endpoint = readiness::udp::endpoint::create(*executor, AF_INET);
        REQUIRE(endpoint.has_value());
        sockaddr_in local_bind {};
        local_bind.sin_family = AF_INET;
        local_bind.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        local_bind.sin_port = htons(0u);
        REQUIRE(::bind(endpoint->raw().get_fd(), reinterpret_cast<const sockaddr*>(&local_bind), sizeof(local_bind)) == 0);

        readiness::knx::udp_transport transport {*endpoint};
        tunnelling_client client {transport, control_binding.address, control_binding.length};
        bool succeeded {};

        executor->spawn(detail::run_readiness_lifecycle(executor, client, endpoint->raw().get_fd(), succeeded));
        std::jthread runner([executor]() { executor->run(); });
        if (runner.joinable())
            runner.join();
        if (server.joinable())
            server.join();

        CHECK(succeeded);
        CHECK(observed.connect_received);
        CHECK(observed.tunnelling_received);
        CHECK(observed.disconnect_received);
    }
#endif
}
