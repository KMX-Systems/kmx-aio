/// @file kmx/aio/knx/secure/server_session_table_test.cpp
/// @brief The KNX IP Secure server session table against the in-tree client session: the handshake, users, limits,
///        timeouts, and the order a wrapper is checked in.
/// @details The client side is the real client session, so every SESSION_RESPONSE the table writes has to verify under the
/// device authentication code, and every wrapper has to open under the key the client derived. Where a test needs a
/// wrapper no well-behaved client would send, the client's key pair is xknx's fixture and the test seals it itself.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/knx/datagram.hpp>
#include <kmx/aio/knx/error.hpp>
#include <kmx/aio/knx/frame.hpp>
#include <kmx/aio/knx/secure/client_session.hpp>
#include <kmx/aio/knx/secure/server_session_table.hpp>
#include <kmx/aio/test/knx/secure_vectors.hpp>
#include <kmx/aio/test/knx/telegram.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <netinet/in.h>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace kmx::aio::test::knx::secure::server_session_table_test
{
    namespace kn = kmx::aio::knx;
    namespace ks = kmx::aio::knx::secure;
    namespace sv = kmx::aio::test::knx::secure_vectors;
    using kn::error;
    using kn::make_error_code;

    namespace detail
    {
        /// @brief A buffer as large as any datagram.
        using buffer_t = std::array<std::uint8_t, kn::frame::max_datagram_size>;

        constexpr ks::serial_number_t client_serial {0x00u, 0xFAu, 0x12u, 0x34u, 0x56u, 0x78u};
        constexpr ks::serial_number_t server_serial {0x00u, 0xFAu, 0xAAu, 0xAAu, 0xAAu, 0xAAu};
        /// @brief The TCP HPAI a SESSION_REQUEST names.
        constexpr kn::hpai tcp_endpoint {kn::ipv4_endpoint {}, 0x02u};
        /// @brief The user the tests authenticate as.
        constexpr std::uint8_t user_id = 2u;

        /// @brief The tunnel addresses that user may use.
        [[nodiscard]] std::vector<kn::individual_address> user_addresses()
        {
            return {kn::individual_address {1u, 1u, 240u}, kn::individual_address {1u, 1u, 241u}};
        }

        /// @brief A connection the table only compares, never uses.
        class idle_connection final: public kn::datagram_transport
        {
        public:
            [[nodiscard]] task_returning_expected_size_t send(cspan_byte_t, const sockaddr*, ::socklen_t) noexcept(false) override
            {
                co_return std::unexpected(make_error_code(error::shutdown));
            }

            [[nodiscard]] task_returning_expected_size_t receive(span_byte_t, kn::transport_peer&) noexcept(false) override
            {
                co_return std::unexpected(make_error_code(error::shutdown));
            }
        };

        /// @brief Hands out xknx's client key pair every time.
        class fixed_key_entropy final: public ks::entropy_source
        {
        public:
            [[nodiscard]] expected_void_t fill(const span_uint8_t destination) noexcept override
            {
                std::ranges::fill(destination, std::uint8_t {});
                return {};
            }

            [[nodiscard]] ks::x25519_key_pair_result_t generate_key_pair() noexcept override
            {
                return ks::x25519_key_pair {
                    .private_key = private_key(),
                    .public_key =
                        sv::fixed<32u>("0a a2 27 b4 fd 7a 32 31 9b a9 96 0a c0 36 ce 0e 5c 45 07 b5 ae 55 16 1f 10 78 b1 dc fb 3c b6 31")};
            }

            [[nodiscard]] static ks::x25519_private_key private_key()
            {
                return ks::x25519_private_key {
                    sv::fixed<32u>("b8 fa bd 62 66 5d 8b 9e 8a 9d 8b 1f 4b ca 42 c8 c2 78 9a 61 10 f5 0e 9d d7 85 b3 ed e8 83 f3 78")};
            }
        };

        template <typename Value>
        [[nodiscard]] std::error_code error_of(const std::expected<Value, std::error_code>& result) noexcept
        {
            return result.has_value() ? std::error_code {} : result.error();
        }

        [[nodiscard]] ks::secret_key derived_user_key(const std::string_view password)
        {
            auto key = ks::derive_user_password_key(password);
            REQUIRE(key.has_value());
            return std::move(*key);
        }

        /// @brief The key of the password "secret", derived once: PBKDF2 is where these tests would spend their time.
        [[nodiscard]] const ks::secret_key& secret_user_key()
        {
            static const auto key = derived_user_key("secret");
            return key;
        }

        /// @brief The key of the device authentication code "trustme", derived once.
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

        [[nodiscard]] std::shared_ptr<const ks::server_configuration> configuration(const std::uint16_t max_sessions = 16u)
        {
            auto value = std::make_shared<ks::server_configuration>();
            value->device_authentication_code = device_code().clone();
            value->users.push_back(
                ks::tunnelling_user {.user_id = user_id, .password_key = secret_user_key().clone(), .tunnel_addresses = user_addresses()});
            value->serial_number = server_serial;
            value->max_sessions = max_sessions;
            return value;
        }

        [[nodiscard]] ks::tunnelling_credentials credentials(const std::uint8_t id = user_id, const std::string_view password = "secret")
        {
            return ks::tunnelling_credentials {.user_id = id,
                                               .user_password_key =
                                                   (password == "secret") ? secret_user_key().clone() : derived_user_key(password),
                                               .device_authentication_code = device_code().clone(),
                                               .serial_number = client_serial};
        }

        /// @brief A client address in 192.0.2.0/24.
        [[nodiscard]] kn::transport_peer peer(const std::uint8_t host, const std::uint16_t port) noexcept
        {
            kn::transport_peer value {};
            auto& address = reinterpret_cast<sockaddr_in&>(value.address);
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(0xC0000200u | host);
            address.sin_port = htons(port);
            value.length = sizeof(sockaddr_in);
            return value;
        }

        [[nodiscard]] std::vector<std::uint8_t> tunnel_frame(const std::uint8_t sequence)
        {
            std::vector<std::uint8_t> packet(sample_tunnelling_packet_size, 0u);
            REQUIRE(kn::frame::encode_tunnelling_request_packet(packet, 1u, sequence, sample_cemi).has_value());
            return packet;
        }

        /// @brief Seals @p plain as a client holding @p key would.
        [[nodiscard]] std::vector<std::uint8_t> seal(const ks::secret_key& key, const std::uint16_t session_id,
                                                     const std::uint64_t sequence, const cspan_uint8_t plain)
        {
            std::vector<std::uint8_t> wire(kn::frame::max_datagram_size, 0u);
            const ks::wrapper_fields fields {.session_id = session_id,
                                             .sequence = ks::encode_sequence(sequence),
                                             .serial_number = client_serial,
                                             .message_tag = ks::tunnelling_message_tag};
            const auto size = ks::seal_wrapper(wire, key, fields, plain);
            REQUIRE(size.has_value());
            wire.resize(*size);
            return wire;
        }

        /// @brief Decodes the wrapper in @p wire.
        [[nodiscard]] ks::secure_wrapper_frame wrapper_in(const cspan_uint8_t wire)
        {
            const auto decoded = kn::decode_datagram(wire);
            REQUIRE(decoded.has_value());
            const auto* const wrapper = std::get_if<ks::secure_wrapper_frame>(&decoded->payload);
            REQUIRE(wrapper != nullptr);
            return *wrapper;
        }

        /// @brief Opens the wrapper in @p wire with the table, as if it arrived on @p connection.
        [[nodiscard]] ks::server_opened_frame_result_t open_wire(ks::server_session_table& table, const kn::datagram_transport& connection,
                                                                 const kn::transport_peer& peer, const cspan_uint8_t wire, buffer_t& plain,
                                                                 const std::uint64_t now_ms)
        {
            return table.open(&connection, peer, wrapper_in(wire), plain, now_ms);
        }

        /// @brief Sends the client's SESSION_REQUEST to the table.
        [[nodiscard]] ks::session_response_result_t request(ks::server_session_table& table, ks::client_session& client,
                                                            const kn::datagram_transport& connection, const kn::transport_peer& from,
                                                            const std::uint64_t now_ms)
        {
            buffer_t wire {};
            const auto size = client.begin(wire, tcp_endpoint, now_ms);
            REQUIRE(size.has_value());
            const auto decoded = ks::decode_session_request_packet({wire.data(), *size});
            REQUIRE(decoded.has_value());
            return table.on_session_request(&connection, from, *decoded, now_ms);
        }

        /// @brief Runs the handshake as far as the table's verdict on SESSION_AUTHENTICATE.
        [[nodiscard]] ks::server_opened_frame_result_t authenticate(ks::server_session_table& table, ks::client_session& client,
                                                                    const kn::datagram_transport& connection,
                                                                    const kn::transport_peer& from, const std::uint64_t now_ms)
        {
            const auto response = request(table, client, connection, from, now_ms);
            REQUIRE(response.has_value());
            buffer_t wire {};
            const auto size = client.on_session_response(*response, wire, now_ms);
            REQUIRE(size.has_value());
            buffer_t plain {};
            return open_wire(table, connection, from, {wire.data(), *size}, plain, now_ms);
        }

        /// @brief Runs the whole handshake, the server's SESSION_STATUS included, and returns the session id.
        [[nodiscard]] std::uint16_t establish(ks::server_session_table& table, ks::client_session& client,
                                              const kn::datagram_transport& connection, const kn::transport_peer& from,
                                              const std::uint64_t now_ms)
        {
            const auto verdict = authenticate(table, client, connection, from, now_ms);
            REQUIRE(verdict.has_value());
            REQUIRE(verdict->kind == ks::server_frame_kind::authenticated);
            buffer_t wire {};
            const auto status = table.seal_status(&connection, verdict->session_id, ks::session_status::authentication_success, wire);
            REQUIRE(status.has_value());
            buffer_t plain {};
            REQUIRE(client.open(wrapper_in({wire.data(), *status}), plain, now_ms).has_value());
            REQUIRE(client.established());
            return verdict->session_id;
        }
    }

    TEST_CASE("knx secure server session table opens a session the in-tree client authenticates", "[knx][secure][server]")
    {
        detail::idle_connection connection {};
        ks::server_session_table table {detail::configuration(), ks::system_entropy()};
        ks::client_session client {detail::credentials(), ks::system_entropy()};
        const auto session_id = detail::establish(table, client, connection, detail::peer(10u, 50'000u), 0u);
        CHECK(session_id != 0u);
        CHECK(session_id == client.session_id());
        CHECK(table.authenticated(session_id));
        CHECK(std::ranges::equal(table.tunnel_addresses(session_id), detail::user_addresses()));

        const auto frame = detail::tunnel_frame(0u);
        detail::buffer_t wire {};
        const auto sealed = client.seal(frame, wire, 1u);
        REQUIRE(sealed.has_value());
        detail::buffer_t plain {};
        const auto opened = detail::open_wire(table, connection, detail::peer(10u, 50'000u), {wire.data(), *sealed}, plain, 1u);
        REQUIRE(opened.has_value());
        CHECK(opened->kind == ks::server_frame_kind::tunnel);
        CHECK(std::ranges::equal(std::span {plain}.first(opened->size), frame));

        const auto answer = detail::tunnel_frame(1u);
        const auto answered = table.seal(&connection, session_id, answer, wire);
        REQUIRE(answered.has_value());
        const auto received = client.open(detail::wrapper_in({wire.data(), *answered}), plain, 2u);
        REQUIRE(received.has_value());
        CHECK(received->for_tunnel);
        CHECK(std::ranges::equal(std::span {plain}.first(received->size), answer));
        CHECK(table.counters().sessions_opened == 1u);
    }

    TEST_CASE("knx secure server session table refuses a wrong password and an unknown user alike", "[knx][secure][server]")
    {
        detail::idle_connection connection {};
        ks::server_session_table table {detail::configuration(), ks::system_entropy()};
        ks::client_session wrong_password {detail::credentials(detail::user_id, "not the password"), ks::system_entropy()};
        ks::client_session unknown_user {detail::credentials(9u), ks::system_entropy()};
        const auto refused = detail::authenticate(table, wrong_password, connection, detail::peer(10u, 50'000u), 0u);
        const auto unknown = detail::authenticate(table, unknown_user, connection, detail::peer(11u, 50'000u), 0u);
        REQUIRE(refused.has_value());
        REQUIRE(unknown.has_value());
        CHECK(refused->kind == ks::server_frame_kind::refused_authentication);
        CHECK(unknown->kind == ks::server_frame_kind::refused_authentication);
        CHECK(!table.authenticated(refused->session_id));
        CHECK(table.tunnel_addresses(refused->session_id).empty());
        CHECK(table.counters().authentication_failures == 2u);

        // Nothing is tunnelled to a session whose user is not authenticated, though it can still be told so.
        detail::buffer_t wire {};
        CHECK(detail::error_of(table.seal(&connection, refused->session_id, detail::tunnel_frame(0u), wire)) ==
              make_error_code(error::secure_session_closed));
        CHECK(table.seal_status(&connection, refused->session_id, ks::session_status::authentication_failed, wire).has_value());
    }

    TEST_CASE("knx secure server session table gives every live session its own id and its own key pair", "[knx][secure][server]")
    {
        detail::idle_connection connection {};
        ks::server_session_table table {detail::configuration(), ks::system_entropy()};
        std::vector<std::unique_ptr<ks::client_session>> clients {};
        std::vector<ks::session_response_frame> responses {};
        for (std::uint8_t host = 1u; host <= 3u; ++host)
        {
            clients.push_back(std::make_unique<ks::client_session>(detail::credentials(), ks::system_entropy()));
            const auto response = detail::request(table, *clients.back(), connection, detail::peer(host, 50'000u), 0u);
            REQUIRE(response.has_value());
            responses.push_back(*response);
        }
        CHECK(std::ranges::none_of(responses, [](const ks::session_response_frame& value) { return value.session_id == 0u; }));
        CHECK(responses[0u].session_id != responses[1u].session_id);
        CHECK(responses[1u].session_id != responses[2u].session_id);
        CHECK(responses[0u].session_id != responses[2u].session_id);
        // A fresh key pair for every session (P3).
        CHECK(responses[0u].server_public_key != responses[1u].server_public_key);

        // An id that has just ended is not handed straight to the next session.
        table.close(responses[1u].session_id);
        ks::client_session next_client {detail::credentials(), ks::system_entropy()};
        const auto next = detail::request(table, next_client, connection, detail::peer(4u, 50'000u), 0u);
        REQUIRE(next.has_value());
        CHECK(std::ranges::none_of(responses,
                                   [&next](const ks::session_response_frame& value) { return value.session_id == next->session_id; }));
        CHECK(table.sessions() == 3u);
    }

    TEST_CASE("knx secure server session table limits unauthenticated sessions per peer address and sessions in total",
              "[knx][secure][server]")
    {
        detail::idle_connection connection {};
        ks::server_session_table table {detail::configuration(4u), ks::system_entropy()};
        std::array<std::unique_ptr<ks::client_session>, 6u> clients {};
        for (auto& client: clients)
            client = std::make_unique<ks::client_session>(detail::credentials(), ks::system_entropy());

        // An authenticated session takes nothing from its peer's share of handshakes.
        const auto verdict = detail::authenticate(table, *clients[0u], connection, detail::peer(1u, 50'000u), 0u);
        REQUIRE(verdict.has_value());
        CHECK(verdict->kind == ks::server_frame_kind::authenticated);
        CHECK(detail::request(table, *clients[1u], connection, detail::peer(1u, 50'001u), 0u).has_value());
        CHECK(detail::request(table, *clients[2u], connection, detail::peer(1u, 50'002u), 0u).has_value());
        // A third handshake from the same address is refused, whatever port it comes from.
        CHECK(detail::error_of(detail::request(table, *clients[3u], connection, detail::peer(1u, 50'003u), 0u)) ==
              make_error_code(error::send_queue_full));
        CHECK(detail::request(table, *clients[4u], connection, detail::peer(2u, 50'000u), 0u).has_value());
        // And past the total, anyone is.
        CHECK(detail::error_of(detail::request(table, *clients[5u], connection, detail::peer(3u, 50'000u), 0u)) ==
              make_error_code(error::send_queue_full));
        CHECK(table.sessions() == 4u);
    }

    TEST_CASE("knx secure server session table reaps a handshake left unfinished and a session nothing arrives from",
              "[knx][secure][server]")
    {
        detail::idle_connection connection {};
        ks::server_session_table table {detail::configuration(), ks::system_entropy()};
        ks::client_session unfinished {detail::credentials(), ks::system_entropy()};
        ks::client_session client {detail::credentials(), ks::system_entropy()};
        const auto pending = detail::request(table, unfinished, connection, detail::peer(1u, 50'000u), 0u);
        REQUIRE(pending.has_value());
        const auto session_id = detail::establish(table, client, connection, detail::peer(2u, 50'000u), 0u);
        CHECK(table.reap(9'999u) == 0u);
        CHECK(table.reap(10'000u) == 1u);
        CHECK(!table.alive(pending->session_id));
        CHECK(table.alive(session_id));

        // A keep-alive keeps the session. What the server sends does not.
        detail::buffer_t wire {};
        const auto keep_alive = client.prepare_keep_alive(wire, 30'000u);
        REQUIRE(keep_alive.has_value());
        detail::buffer_t plain {};
        const auto kept = detail::open_wire(table, connection, detail::peer(2u, 50'000u), {wire.data(), *keep_alive}, plain, 30'000u);
        REQUIRE(kept.has_value());
        CHECK(kept->kind == ks::server_frame_kind::keep_alive);
        CHECK(table.seal(&connection, session_id, detail::tunnel_frame(0u), wire).has_value());
        CHECK(table.reap(89'999u) == 0u);
        CHECK(table.reap(90'000u) == 1u);
        CHECK(table.counters().sessions_timed_out == 2u);
    }

    TEST_CASE("knx secure server session table checks a wrapper's connection, MAC and sequence before what it carries",
              "[knx][secure][server]")
    {
        detail::idle_connection connection {};
        detail::idle_connection other {};
        ks::server_session_table table {detail::configuration(), ks::system_entropy()};
        detail::fixed_key_entropy entropy {};
        ks::client_session client {detail::credentials(), entropy};
        const auto response = detail::request(table, client, connection, detail::peer(1u, 50'000u), 0u);
        REQUIRE(response.has_value());
        // xknx's key pair lets the test seal what no client would send: a tunnel frame before the user is authenticated.
        const auto key = ks::derive_session_key(detail::fixed_key_entropy::private_key(), response->server_public_key);
        REQUIRE(key.has_value());
        const auto early = detail::seal(*key, response->session_id, 0u, detail::tunnel_frame(0u));
        auto forged = early;
        forged.back() ^= 0x01u;

        detail::buffer_t plain {};
        CHECK(detail::error_of(detail::open_wire(table, other, detail::peer(1u, 50'000u), early, plain, 0u)) ==
              make_error_code(error::secure_authentication_failed));
        CHECK(detail::error_of(detail::open_wire(table, connection, detail::peer(2u, 50'000u), early, plain, 0u)) ==
              make_error_code(error::secure_authentication_failed));
        CHECK(detail::error_of(detail::open_wire(table, connection, detail::peer(1u, 50'000u), forged, plain, 0u)) ==
              make_error_code(error::secure_authentication_failed));
        CHECK(detail::error_of(detail::open_wire(table, connection, detail::peer(1u, 50'000u), early, plain, 0u)) ==
              make_error_code(error::unsupported_service));
        CHECK(detail::error_of(detail::open_wire(table, connection, detail::peer(1u, 50'000u), early, plain, 0u)) ==
              make_error_code(error::secure_replay));
        CHECK(table.counters().authentication_failures == 3u);
        CHECK(table.counters().replays == 1u);
        CHECK(table.counters().refused_services == 1u);
        CHECK(!table.authenticated(response->session_id));
    }

    TEST_CASE("knx secure server session table ends a session on SESSION_STATUS close and every session of a closed connection",
              "[knx][secure][server]")
    {
        detail::idle_connection connection {};
        detail::idle_connection other {};
        ks::server_session_table table {detail::configuration(), ks::system_entropy()};
        ks::client_session closing {detail::credentials(), ks::system_entropy()};
        ks::client_session staying {detail::credentials(), ks::system_entropy()};
        ks::client_session elsewhere {detail::credentials(), ks::system_entropy()};
        const auto session_id = detail::establish(table, closing, connection, detail::peer(1u, 50'000u), 0u);
        static_cast<void>(detail::establish(table, staying, connection, detail::peer(1u, 50'001u), 0u));
        const auto kept = detail::establish(table, elsewhere, other, detail::peer(2u, 50'000u), 0u);

        detail::buffer_t wire {};
        const auto close = closing.prepare_close(wire, 1u);
        REQUIRE(close.has_value());
        detail::buffer_t plain {};
        const auto closed = detail::open_wire(table, connection, detail::peer(1u, 50'000u), {wire.data(), *close}, plain, 1u);
        REQUIRE(closed.has_value());
        CHECK(closed->kind == ks::server_frame_kind::closed);
        CHECK(!table.alive(session_id));
        CHECK(table.counters().sessions_closed == 1u);

        CHECK(table.close_connection(&connection) == 1u);
        CHECK(!table.has_sessions(&connection));
        CHECK(table.has_sessions(&other));
        CHECK(table.alive(kept));
    }

    TEST_CASE("knx secure server session table refuses a session under an all-zero serial number", "[knx][secure][server]")
    {
        detail::idle_connection connection {};
        auto configuration = std::make_shared<ks::server_configuration>();
        configuration->device_authentication_code = detail::device_code().clone();
        ks::server_session_table table {configuration, ks::system_entropy()};
        ks::client_session client {detail::credentials(), ks::system_entropy()};
        // There is no default serial number: a server configured without one answers no SESSION_REQUEST (P8).
        CHECK(detail::error_of(detail::request(table, client, connection, detail::peer(1u, 50'000u), 0u)) ==
              make_error_code(error::invalid_configuration));
        CHECK(table.sessions() == 0u);
    }
}
