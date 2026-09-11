/// @file src/kmx/aio/knx/secure/tunnelling_client_secure_test.cpp
/// @brief The tunnelling client over a KNX IP Secure session.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details Covers the handshake before anything else, every frame wrapped, no downgrade, keep-alives, and a fresh
/// session on every reconnect.
/// A stand-in server plays the other end over a fake stream transport, keyed from xknx's fixture. It answers
/// SESSION_REQUEST with a genuine SESSION_RESPONSE, opens every wrapper the client sends, and answers the tunnel frames
/// inside them under wrappers of its own.
#ifndef PCH
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/completion/timer.hpp>
    #include <kmx/aio/knx/datagram.hpp>
    #include <kmx/aio/knx/secure/session.hpp>
    #include <kmx/aio/knx/secure/wrapper.hpp>
    #include <kmx/aio/knx/tunnelling_client.hpp>
    #include <kmx/aio/test/knx/recording_transport.hpp>
    #include <kmx/aio/test/knx/secure_vectors.hpp>
    #include <kmx/aio/test/knx/telegram.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <algorithm>
    #include <array>
    #include <chrono>
    #include <cstdint>
    #include <deque>
    #include <variant>
    #include <vector>
    #include <netinet/in.h>
#endif

namespace kmx::aio::test::knx::secure::tunnelling_client_secure_test
{
    namespace kn = kmx::aio::knx;
    namespace ks = kmx::aio::knx::secure;
    namespace sv = kmx::aio::test::knx::secure_vectors;
    using kn::error;
    using kn::make_error_code;

    namespace detail
    {
        constexpr ks::serial_number_t own_serial {0x00u, 0xFAu, 0x12u, 0x34u, 0x56u, 0x78u};
        constexpr ks::serial_number_t server_serial {0x00u, 0xFAu, 0xAAu, 0xAAu, 0xAAu, 0xAAu};
        /// @brief The tunnelling channel the stand-in server allocates.
        constexpr std::uint8_t channel = 7u;

        /// @brief The time the session's keep-alive and timeout read.
        std::uint64_t session_now_ms {};

        [[nodiscard]] std::uint64_t session_clock() noexcept
        {
            return session_now_ms;
        }

        /// @brief xknx's session fixture.
        struct handshake_fixture
        {
            ks::x25519_private_key client_private {
                sv::fixed<32u>("b8 fa bd 62 66 5d 8b 9e 8a 9d 8b 1f 4b ca 42 c8 c2 78 9a 61 10 f5 0e 9d d7 85 b3 ed e8 83 f3 78")};
            ks::x25519_public_key_t client_public {
                sv::fixed<32u>("0a a2 27 b4 fd 7a 32 31 9b a9 96 0a c0 36 ce 0e 5c 45 07 b5 ae 55 16 1f 10 78 b1 dc fb 3c b6 31")};
            ks::x25519_public_key_t server_public {
                sv::fixed<32u>("bd f0 99 90 99 23 14 3e f0 a5 de 0b 3b e3 68 7b c5 bd 3c f5 f9 e6 f9 01 69 9c d8 70 ec 1f f8 24")};
        };

        /// @brief Hands out the fixture's key pair, counting how often one was asked for.
        class fixed_key_entropy final: public ks::entropy_source
        {
        public:
            explicit fixed_key_entropy(const handshake_fixture& fixture) noexcept: fixture_(fixture) {}

            /// @brief How many key pairs were drawn.
            std::size_t pairs {};

            [[nodiscard]] expected_void_t fill(const span_uint8_t destination) noexcept override
            {
                std::ranges::fill(destination, std::uint8_t {});
                return {};
            }

            [[nodiscard]] ks::x25519_key_pair_result_t generate_key_pair() noexcept override
            {
                ++pairs;
                return ks::x25519_key_pair {.private_key = fixture_.client_private.clone(), .public_key = fixture_.client_public};
            }

        private:
            const handshake_fixture& fixture_;
        };

        template <typename Value>
        [[nodiscard]] std::error_code error_of(const expected_t<Value>& result) noexcept
        {
            return result.has_value() ? std::error_code {} : result.error();
        }

        [[nodiscard]] ks::tunnelling_credentials credentials()
        {
            auto user_key = ks::derive_user_password_key("secret");
            auto device_code = ks::derive_device_authentication_code("trustme");
            REQUIRE(user_key.has_value());
            REQUIRE(device_code.has_value());
            return ks::tunnelling_credentials {.user_id = 1u,
                                               .user_password_key = std::move(*user_key),
                                               .device_authentication_code = std::move(*device_code),
                                               .serial_number = own_serial};
        }

        [[nodiscard]] std::uint16_t service_of(const std::vector<std::uint8_t>& wire) noexcept
        {
            return (wire.size() >= 4u) ? static_cast<std::uint16_t>((wire[2u] << 8u) | wire[3u]) : std::uint16_t {};
        }

        [[nodiscard]] std::vector<std::uint16_t> services(const std::vector<std::vector<std::uint8_t>>& frames)
        {
            std::vector<std::uint16_t> result {};
            for (const auto& frame: frames)
                result.push_back(service_of(frame));
            return result;
        }

        [[nodiscard]] std::vector<std::uint8_t> indication(const std::uint8_t sequence)
        {
            std::vector<std::uint8_t> wire(sample_tunnelling_packet_size, 0u);
            REQUIRE(kn::frame::encode_tunnelling_request_packet(wire, channel, sequence, sample_cemi).has_value());
            return wire;
        }

        [[nodiscard]] sockaddr_in server_address() noexcept
        {
            sockaddr_in address {};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons(3671u);
            return address;
        }

        /// @brief How the stand-in server behaves.
        enum class server_mode : std::uint8_t
        {
            /// @brief Answers everything properly.
            cooperative,
            /// @brief Answers SESSION_REQUEST with a MAC that does not verify.
            forged_response,
            /// @brief Refuses the user after SESSION_AUTHENTICATE.
            refused_authentication,
            /// @brief Never answers SESSION_REQUEST.
            silent,
        };

        /// @brief A stand-in KNX IP Secure tunnelling server behind a fake stream transport.
        class stand_in_transport final: public recording_transport
        {
        public:
            explicit stand_in_transport(const handshake_fixture& fixture): fixture_(fixture)
            {
                auto key = ks::derive_session_key(fixture.client_private, fixture.server_public);
                REQUIRE(key.has_value());
                session_key_ = std::move(*key);
            }

            /// @brief The loop a receive with no deadline waits on.
            completion::executor* wait_executor {};
            /// @brief How the server behaves.
            server_mode mode {server_mode::cooperative};
            /// @brief Whether the transport reports itself a stream.
            bool stream = true;
            /// @brief Whether a receive is waiting for a frame at this moment.
            bool receiving {};
            std::size_t opens {};
            std::size_t closes {};
            /// @brief What the client sent inside wrappers, opened, in order.
            std::vector<std::vector<std::uint8_t>> unwrapped {};
            /// @brief What the client sent without a wrapper, in order.
            std::vector<std::vector<std::uint8_t>> clear {};

            /// @brief Queues a frame to the client under a wrapper of the server's.
            void push_wrapped(const cspan_uint8_t plain) { frames_.push_back(wrap(plain)); }

            /// @brief Queues a frame to the client as it is.
            void push_clear(const cspan_uint8_t wire) { frames_.emplace_back(wire.begin(), wire.end()); }

            [[nodiscard]] bool stream_oriented() const noexcept override { return stream; }

            [[nodiscard]] task_returning_expected_void_t open() noexcept(false) override
            {
                ++opens;
                open_ = true;
                server_sequence_ = 0u;
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
                const cspan_uint8_t wire {reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()};
                if (const auto decoded = kn::decode_datagram(wire); decoded.has_value())
                    handle(*decoded, wire);
                co_return expected_size_t {payload.size()};
            }

            [[nodiscard]] task_returning_expected_size_t receive(const span_byte_t buffer,
                                                                 kn::transport_peer& peer) noexcept(false) override
            {
                receiving = true;
                for (auto waits = 0u; frames_.empty() && (wait_executor != nullptr) && (waits < 2'000u); ++waits)
                {
                    completion::timer timer {*wait_executor};
                    static_cast<void>(co_await timer.wait(std::chrono::milliseconds {1}));
                }

                receiving = false;
                co_return take(buffer, peer);
            }

            [[nodiscard]] task_returning_expected_size_t receive_until(const span_byte_t buffer, kn::transport_peer& peer,
                                                                       const std::uint32_t) noexcept(false) override
            {
                co_return take(buffer, peer);
            }

        private:
            void handle(const kn::datagram& value, const cspan_uint8_t wire)
            {
                if (const auto* const request = std::get_if<ks::session_request_frame>(&value.payload))
                {
                    clear.emplace_back(wire.begin(), wire.end());
                    answer_request(*request);
                    return;
                }

                const auto* const wrapper = std::get_if<ks::wrapper_frame>(&value.payload);
                if (wrapper == nullptr)
                {
                    clear.emplace_back(wire.begin(), wire.end());
                    return;
                }

                std::vector<std::uint8_t> plain(kn::frame::max_datagram_size, 0u);
                const auto size = ks::open_wrapper(plain, session_key_, *wrapper);
                REQUIRE(size.has_value());
                plain.resize(*size);
                unwrapped.push_back(plain);
                answer_wrapped(service_of(plain));
            }

            void answer_request(const ks::session_request_frame& request)
            {
                if (mode == server_mode::silent)
                    return;
                const auto device_code = ks::derive_device_authentication_code("trustme");
                REQUIRE(device_code.has_value());
                auto mac = ks::session_response_mac(*device_code, 1u, request.client_public_key, fixture_.server_public);
                REQUIRE(mac.has_value());
                if (mode == server_mode::forged_response)
                    (*mac)[0u] ^= 0x01u;
                std::vector<std::uint8_t> response(ks::session_response_size, 0u);
                REQUIRE(ks::encode_session_response_packet(response,
                                                           {.session_id = 1u, .server_public_key = fixture_.server_public, .mac = *mac})
                            .has_value());
                frames_.push_back(std::move(response));
            }

            void answer_wrapped(const std::uint16_t service)
            {
                if (service == ks::session_authenticate_service)
                    push_wrapped(status(mode == server_mode::refused_authentication ? ks::session_status::authentication_failed :
                                                                                      ks::session_status::authentication_success));
                else if (service == kn::connection::connect_request_service)
                    push_wrapped(connect_response());
                else if (service == kn::connection::connectionstate_request_service)
                    push_wrapped(control_response(kn::connection::connectionstate_response_service));
                else if (service == kn::connection::disconnect_request_service)
                    push_wrapped(control_response(kn::connection::disconnect_response_service));
            }

            [[nodiscard]] static std::vector<std::uint8_t> status(const ks::session_status value)
            {
                std::vector<std::uint8_t> wire(ks::session_status_size, 0u);
                REQUIRE(ks::encode_session_status_packet(wire, {value}).has_value());
                return wire;
            }

            [[nodiscard]] static std::vector<std::uint8_t> connect_response()
            {
                std::vector<std::uint8_t> wire(20u, 0u);
                const kn::connect_response_frame value {.channel_id = channel,
                                                        .status = kn::connect_status::no_error,
                                                        .data_endpoint = kn::hpai {{}, 0x02u},
                                                        .assigned_address = kn::individual_address {1u, 1u, 20u}};
                REQUIRE(kn::connection::encode_connect_response_packet(wire, value).has_value());
                return wire;
            }

            [[nodiscard]] static std::vector<std::uint8_t> control_response(const std::uint16_t service)
            {
                std::vector<std::uint8_t> wire(8u, 0u);
                const auto encoded = (service == kn::connection::connectionstate_response_service) ?
                                         kn::connection::encode_connectionstate_response_packet(
                                             wire, kn::connectionstate_response_frame {channel, kn::connect_status::no_error}) :
                                         kn::connection::encode_disconnect_response_packet(
                                             wire, kn::disconnect_response_frame {channel, kn::connect_status::no_error});
                REQUIRE(encoded.has_value());
                return wire;
            }

            [[nodiscard]] std::vector<std::uint8_t> wrap(const cspan_uint8_t plain)
            {
                std::vector<std::uint8_t> wire(kn::frame::max_datagram_size, 0u);
                const ks::wrapper_fields fields {.session_id = 1u,
                                                 .sequence = ks::encode_sequence(server_sequence_++),
                                                 .serial_number = server_serial,
                                                 .message_tag = {0x00u, 0x00u}};
                const auto size = ks::seal_wrapper(wire, session_key_, fields, plain);
                REQUIRE(size.has_value());
                wire.resize(*size);
                return wire;
            }

            [[nodiscard]] expected_size_t take(const span_byte_t buffer, kn::transport_peer& peer)
            {
                if (frames_.empty())
                    return std::unexpected(make_error_code(error::timeout));
                const auto next = std::move(frames_.front());
                frames_.pop_front();
                fill_peer(peer, false, INADDR_LOOPBACK, 3671u);
                return deliver(next, buffer);
            }

            const handshake_fixture& fixture_;
            ks::secret_key session_key_ {};
            std::uint64_t server_sequence_ {};
            std::deque<std::vector<std::uint8_t>> frames_ {};
            bool open_ {};
        };

        /// @brief The keys, the stand-in server, the secure client and the loop each test starts from.
        struct session_fixture
        {
            handshake_fixture keys {};
            fixed_key_entropy entropy {keys};
            stand_in_transport transport {keys};
            sockaddr_in server = server_address();
            kn::tunnelling_client client {transport,
                                          reinterpret_cast<const sockaddr*>(&server),
                                          sizeof(server),
                                          {.credentials = credentials(), .clock_ms = session_clock, .entropy = &entropy}};
            completion::executor executor;

            session_fixture()
            {
                session_now_ms = 0u;
                transport.wait_executor = &executor;
            }

            void spawn_and_run(task<void> work) noexcept(false)
            {
                executor.spawn(std::move(work));
                executor.run();
            }
        };
    }

    TEST_CASE_METHOD(detail::session_fixture, "knx secure tunnelling client runs the handshake first and wraps everything after it",
                     "[knx][secure][tunnelling][unit]")
    {
        bool connected {};
        bool sent {};
        bool received {};
        bool disconnected {};
        auto run = [&]() -> task<void>
        {
            connected = (co_await client.connect(kn::connect_request_frame {})).has_value();
            if (connected)
            {
                sent = (co_await client.send(sample_cemi)).has_value();
                transport.push_wrapped(detail::indication(0u));
                const auto cemi = co_await client.receive_cemi();
                received = cemi.has_value() && std::ranges::equal(*cemi, sample_cemi);
                disconnected = (co_await client.disconnect()).has_value();
            }

            executor.stop();
        };
        spawn_and_run(run());

        CHECK(client.is_secure());
        CHECK(connected);
        CHECK(sent);
        CHECK(received);
        CHECK(disconnected);
        // The one frame that ever left in the clear is SESSION_REQUEST; everything after it went wrapped.
        CHECK(detail::services(transport.clear) == std::vector<std::uint16_t> {ks::session_request_service});
        CHECK(detail::services(transport.unwrapped) ==
              std::vector<std::uint16_t> {ks::session_authenticate_service, kn::connection::connect_request_service,
                                          kn::frame::tunnelling_request_service, kn::connection::disconnect_request_service,
                                          ks::session_status_service});
        const auto close = ks::decode_session_status_packet(transport.unwrapped.back());
        REQUIRE(close.has_value());
        CHECK(close->status == ks::session_status::close);
        CHECK(transport.closes == 1u);
        CHECK(client.secure_counters().sessions_opened == 1u);
        CHECK(client.secure_counters().sessions_closed == 1u);
    }

    TEST_CASE("knx secure tunnelling client never sends a CONNECT when the handshake fails", "[knx][secure][tunnelling][unit]")
    {
        struct expectation
        {
            detail::server_mode mode;
            error failure;
        };
        for (const auto& [mode, failure]: {expectation {detail::server_mode::forged_response, error::secure_authentication_failed},
                                           expectation {detail::server_mode::refused_authentication, error::secure_session_rejected},
                                           expectation {detail::server_mode::silent, error::timeout}})
        {
            detail::session_fixture fixture {};
            fixture.transport.mode = mode;
            std::error_code outcome {};
            auto run = [&]() -> task<void>
            {
                outcome = detail::error_of(co_await fixture.client.connect(kn::connect_request_frame {}));
                fixture.executor.stop();
            };
            fixture.spawn_and_run(run());

            CHECK(outcome == make_error_code(failure));
            CHECK(!fixture.client.connected());
            CHECK(fixture.transport.closes == 1u);
            // No downgrade: nothing but SESSION_REQUEST in the clear, and no CONNECT_REQUEST in any form.
            CHECK(detail::services(fixture.transport.clear) == std::vector<std::uint16_t> {ks::session_request_service});
            const auto sent_wrapped = detail::services(fixture.transport.unwrapped);
            CHECK(std::ranges::find(sent_wrapped, kn::connection::connect_request_service) == sent_wrapped.end());
        }
    }

    TEST_CASE_METHOD(detail::session_fixture, "knx secure tunnelling client refuses an unencrypted tunnelling request",
                     "[knx][secure][tunnelling][unit]")
    {
        std::error_code refused {};
        bool received_after {};
        auto run = [&]() -> task<void>
        {
            if ((co_await client.connect(kn::connect_request_frame {})).has_value())
            {
                transport.push_clear(detail::indication(0u));
                refused = detail::error_of(co_await client.receive_cemi());
                // The refused frame is that frame's problem: the tunnel carries on.
                transport.push_wrapped(detail::indication(1u));
                received_after = (co_await client.receive_cemi()).has_value();
            }

            executor.stop();
        };
        spawn_and_run(run());

        CHECK(refused == make_error_code(error::secure_frame_required));
        CHECK(client.secure_counters().unencrypted_refused == 1u);
        CHECK(received_after);
        CHECK(client.connected());
    }

    TEST_CASE_METHOD(detail::session_fixture, "knx secure tunnelling client keeps its session alive on a quiet bus",
                     "[knx][secure][tunnelling][unit]")
    {
        bool connected {};
        bool received {};
        bool receive_done {};
        bool every_poll_quiet = true;
        std::size_t keep_alives {};
        auto receiver = [&]() -> task<void>
        {
            received = (co_await client.receive_cemi()).has_value();
            receive_done = true;
        };
        auto run = [&]() -> task<void>
        {
            connected = (co_await client.connect(kn::connect_request_frame {})).has_value();
            if (!connected)
            {
                executor.stop();
                co_return;
            }

            executor.spawn(receiver());
            // Five minutes of session time while one task waits for a telegram that does not come.
            for (std::uint64_t now = 10'000u; now <= 300'000u; now += 10'000u)
            {
                detail::session_now_ms = now;
                if (client.keep_alive_due() && (co_await client.keep_alive()).has_value())
                    ++keep_alives;
                every_poll_quiet = client.poll().has_value() && every_poll_quiet;
                completion::timer timer {executor};
                static_cast<void>(co_await timer.wait(std::chrono::milliseconds {1}));
            }

            transport.push_wrapped(detail::indication(0u));
            for (auto waits = 0u; !receive_done && (waits < 1'000u); ++waits)
            {
                completion::timer timer {executor};
                static_cast<void>(co_await timer.wait(std::chrono::milliseconds {1}));
            }

            executor.stop();
        };
        spawn_and_run(run());

        CHECK(connected);
        CHECK(every_poll_quiet);
        CHECK(keep_alives == 6u);
        CHECK(received);
        CHECK(client.connected());
        CHECK(client.secure_counters().sessions_timed_out == 0u);
    }

    TEST_CASE_METHOD(detail::session_fixture, "knx secure tunnelling client ends a session that went silent",
                     "[knx][secure][tunnelling][unit]")
    {
        bool connected {};
        auto run = [&]() -> task<void>
        {
            connected = (co_await client.connect(kn::connect_request_frame {})).has_value();
            executor.stop();
        };
        spawn_and_run(run());
        REQUIRE(connected);

        detail::session_now_ms = 59'999u;
        CHECK(client.poll().has_value());
        detail::session_now_ms = 60'000u;
        CHECK(detail::error_of(client.poll()) == make_error_code(error::secure_session_closed));
        CHECK(client.closed());
        CHECK(transport.closes == 1u);
        CHECK(client.secure_counters().sessions_timed_out == 1u);
    }

    TEST_CASE_METHOD(detail::session_fixture, "knx secure tunnelling client reconnects through a fresh session",
                     "[knx][secure][tunnelling][unit]")
    {
        bool reconnected {};
        auto run = [&]() -> task<void>
        {
            if ((co_await client.connect(kn::connect_request_frame {})).has_value())
                static_cast<void>(co_await client.disconnect());
            client.reset();
            reconnected = (co_await client.connect(kn::connect_request_frame {})).has_value();
            executor.stop();
        };
        spawn_and_run(run());

        CHECK(reconnected);
        CHECK(entropy.pairs == 2u);
        CHECK(transport.opens == 2u);
        // The second session's first wrapper starts the sequence again: no key and no sequence number is reused (P3).
        const auto& sent = transport.sent_packets();
        const auto second_request = std::ranges::find_if(
            std::next(std::ranges::find_if(sent, [](const auto& wire) { return detail::service_of(wire) == ks::session_request_service; })),
            sent.end(), [](const auto& wire) { return detail::service_of(wire) == ks::session_request_service; });
        REQUIRE(second_request != sent.end());
        const auto authenticate = std::next(second_request);
        REQUIRE(authenticate != sent.end());
        const auto wrapper = ks::decode_wrapper_packet(*authenticate);
        REQUIRE(wrapper.has_value());
        CHECK(ks::decode_sequence(wrapper->sequence) == 0u);
    }

    TEST_CASE("knx secure tunnelling client refuses a datagram transport", "[knx][secure][tunnelling][unit]")
    {
        detail::session_fixture fixture {};
        fixture.transport.stream = false;
        std::error_code outcome {};
        auto run = [&]() -> task<void>
        {
            outcome = detail::error_of(co_await fixture.client.connect(kn::connect_request_frame {}));
            fixture.executor.stop();
        };
        fixture.spawn_and_run(run());

        CHECK(outcome == make_error_code(error::invalid_configuration));
        CHECK(fixture.transport.sent_packets().empty());
    }
}
