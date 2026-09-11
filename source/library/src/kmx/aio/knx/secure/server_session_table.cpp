/// @file kmx/aio/knx/secure/server_session_table.cpp
/// @brief The compiled body of the KNX IP Secure server session table.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/server_session_table.hpp>

#include <kmx/aio/knx/error.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <netinet/in.h>
#include <utility>

namespace kmx::aio::knx::secure
{
    [[nodiscard]] static std::unexpected<std::error_code> refuse(const error reason) noexcept
    {
        return std::unexpected(make_error_code(reason));
    }

    /// @brief Indicates whether two peers share an address, whatever their ports: each new connection from one client
    ///        comes from a new port.
    [[nodiscard]] static bool same_address(const transport_peer& lhs, const transport_peer& rhs) noexcept
    {
        if (lhs.address.ss_family != rhs.address.ss_family)
            return false;
        if ((lhs.address.ss_family == AF_INET) && (lhs.length >= sizeof(sockaddr_in)) && (rhs.length >= sizeof(sockaddr_in)))
            return reinterpret_cast<const sockaddr_in&>(lhs.address).sin_addr.s_addr ==
                   reinterpret_cast<const sockaddr_in&>(rhs.address).sin_addr.s_addr;
        if ((lhs.address.ss_family == AF_INET6) && (lhs.length >= sizeof(sockaddr_in6)) && (rhs.length >= sizeof(sockaddr_in6)))
            return std::memcmp(&reinterpret_cast<const sockaddr_in6&>(lhs.address).sin6_addr,
                               &reinterpret_cast<const sockaddr_in6&>(rhs.address).sin6_addr, sizeof(in6_addr)) == 0;
        return false;
    }

    /// @brief Indicates whether two peers are the very same transport endpoint.
    [[nodiscard]] static bool same_peer(const transport_peer& lhs, const transport_peer& rhs) noexcept
    {
        return (lhs.length == rhs.length) && (lhs.address.ss_family == rhs.address.ss_family) &&
               (std::memcmp(&lhs.address, &rhs.address, static_cast<std::size_t>(lhs.length)) == 0);
    }

    server_session_table::server_session_table(std::shared_ptr<const server_configuration> configuration,
                                               entropy_source& entropy) noexcept(false):
        configuration_(std::move(configuration)),
        entropy_(entropy)
    {
        // Room for every session the table may hold is taken now, so opening one never allocates.
        sessions_.reserve(session_limit());
    }

    std::size_t server_session_table::session_limit() const noexcept
    {
        return std::min<std::size_t>(configuration_->max_sessions, max_server_sessions);
    }

    expected_void_t server_session_table::admit_request(const transport_peer& peer) const noexcept
    {
        if (sessions_.size() >= session_limit())
            return refuse(error::send_queue_full);
        // One peer cannot take every session with handshakes it never finishes.
        const auto unauthenticated = std::ranges::count_if(sessions_, [&peer](const entry& value) noexcept
                                                           { return (value.user == nullptr) && same_address(value.peer, peer); });
        if (static_cast<std::size_t>(unauthenticated) >= configuration_->max_unauthenticated_per_peer)
            return refuse(error::send_queue_full);
        return {};
    }

    std::uint16_t server_session_table::allocate_session_id() noexcept
    {
        // Never zero, which routing uses, and never an id a live session holds. The ids count on rather than starting
        // over, so an id that has just ended is not handed straight to another client.
        for (;;)
        {
            const auto candidate = next_session_id_;
            next_session_id_ = (next_session_id_ == 0xFFFFu) ? std::uint16_t {1u} : static_cast<std::uint16_t>(next_session_id_ + 1u);
            if (find(candidate) == nullptr)
                return candidate;
        }
    }

    session_response_result_t server_session_table::on_session_request(const datagram_transport* const connection,
                                                                       const transport_peer& peer, const session_request_frame& request,
                                                                       const std::uint64_t now_ms) noexcept
    {
        const std::lock_guard lock {mutex_};
        // Every wrapper the session sends carries the server's serial number, so a server with none opens no session (P8).
        if (!valid_serial_number(configuration_->serial_number))
            return refuse(error::invalid_configuration);
        if (const auto admitted = admit_request(peer); !admitted.has_value())
            return std::unexpected(admitted.error());
        // A fresh key pair per session: a password that leaks later opens no session recorded before it (P3).
        const auto key_pair = entropy_.generate_key_pair();
        if (!key_pair.has_value())
            return std::unexpected(key_pair.error());
        auto session_key = derive_session_key(key_pair->private_key, request.client_public_key);
        if (!session_key.has_value())
            return std::unexpected(session_key.error());

        const auto session_id = allocate_session_id();
        const auto mac =
            session_response_mac(configuration_->device_authentication_code, session_id, request.client_public_key, key_pair->public_key);
        if (!mac.has_value())
            return std::unexpected(mac.error());
        sessions_.push_back(entry {.session_id = session_id,
                                   .connection = connection,
                                   .peer = peer,
                                   .client_public_key = request.client_public_key,
                                   .server_public_key = key_pair->public_key,
                                   .session_key = std::move(*session_key),
                                   .created_ms = now_ms,
                                   .last_received_ms = now_ms});
        return session_response_frame {.session_id = session_id, .server_public_key = key_pair->public_key, .mac = *mac};
    }

    server_session_table::entry* server_session_table::find(const std::uint16_t session_id) noexcept
    {
        const auto found = std::ranges::find(sessions_, session_id, &entry::session_id);
        return (found == sessions_.end()) ? nullptr : &*found;
    }

    const server_session_table::entry* server_session_table::find(const std::uint16_t session_id) const noexcept
    {
        const auto found = std::ranges::find(sessions_, session_id, &entry::session_id);
        return (found == sessions_.end()) ? nullptr : &*found;
    }

    const tunnelling_user* server_session_table::find_user(const std::uint8_t user_id) const noexcept
    {
        const auto found = std::ranges::find(configuration_->users, user_id, &tunnelling_user::user_id);
        return (found == configuration_->users.end()) ? nullptr : &*found;
    }

    expected_void_t server_session_table::admit(entry& session, const secure_wrapper_frame& wrapper, const span_uint8_t plain) noexcept
    {
        // With the MAC verified the sequence number can be believed, and it has to move forward (P2).
        const auto sequence = decode_sequence(wrapper.sequence);
        if (session.last_received_sequence.has_value() && (sequence <= *session.last_received_sequence))
        {
            ++counters_.replays;
            detail::cleanse(plain);
            return refuse(error::secure_replay);
        }
        if (const auto header = check_wrapped_frame(plain); !header.has_value())
        {
            ++counters_.refused_services;
            detail::cleanse(plain);
            return std::unexpected(header.error());
        }
        session.last_received_sequence = sequence;
        return {};
    }

    server_opened_frame_result_t server_session_table::open(const datagram_transport* const connection, const transport_peer& peer,
                                                            const secure_wrapper_frame& wrapper, const span_uint8_t destination,
                                                            const std::uint64_t now_ms) noexcept
    {
        const std::lock_guard lock {mutex_};
        // A session belongs to the connection and peer it was opened on: its id arriving elsewhere names nothing.
        auto* const session = find(wrapper.session_id);
        if ((session == nullptr) || (session->connection != connection) || !same_peer(session->peer, peer))
        {
            ++counters_.authentication_failures;
            return refuse(error::secure_authentication_failed);
        }
        const auto opened = open_wrapper(destination, session->session_key, wrapper);
        if (!opened.has_value() && (opened.error() == make_error_code(error::secure_authentication_failed)))
            ++counters_.authentication_failures;
        if (!opened.has_value())
            return std::unexpected(opened.error());
        const span_uint8_t plain {destination.data(), *opened};
        if (const auto admitted = admit(*session, wrapper, plain); !admitted.has_value())
            return std::unexpected(admitted.error());

        session->last_received_ms = now_ms;
        return apply(*session, plain);
    }

    server_opened_frame server_session_table::authenticate(entry& session, const session_authenticate_frame& value) noexcept
    {
        // An unknown user and a wrong password are refused alike, and at the same cost, so the answer does not say which
        // user ids exist.
        const auto* const user = find_user(value.user_id);
        const auto& key = (user != nullptr) ? user->password_key : configuration_->device_authentication_code;
        const auto verified = verify_session_authenticate(key, value, session.client_public_key, session.server_public_key);
        if ((user == nullptr) || !verified.has_value())
        {
            ++counters_.authentication_failures;
            return server_opened_frame {.session_id = session.session_id, .kind = server_frame_kind::refused_authentication};
        }
        session.user = user;
        ++counters_.sessions_opened;
        return server_opened_frame {.session_id = session.session_id, .kind = server_frame_kind::authenticated};
    }

    server_opened_frame_result_t server_session_table::apply_status(entry& session, const session_status status) noexcept
    {
        const auto session_id = session.session_id;
        if (status == session_status::keepalive)
            return server_opened_frame {.session_id = session_id, .kind = server_frame_kind::keep_alive};
        // Close, timeout and unauthenticated all end a session; the rest are the server's to send, not to receive.
        if ((status != session_status::close) && (status != session_status::timeout) && (status != session_status::unauthenticated))
        {
            ++counters_.refused_services;
            return refuse(error::unsupported_service);
        }
        ++counters_.sessions_closed;
        erase(session_id);
        return server_opened_frame {.session_id = session_id, .kind = server_frame_kind::closed};
    }

    server_opened_frame_result_t server_session_table::apply(entry& session, const cspan_uint8_t plain) noexcept
    {
        // SESSION_STATUS is the session's own, whether or not its user is authenticated yet.
        if (const auto status = decode_session_status_packet(plain); status.has_value())
            return apply_status(session, status->status);
        if (session.user != nullptr)
            return server_opened_frame {.session_id = session.session_id, .size = plain.size(), .kind = server_frame_kind::tunnel};

        // Until its user is authenticated a session carries SESSION_AUTHENTICATE and nothing else.
        const auto authentication = decode_session_authenticate_packet(plain);
        if (!authentication.has_value())
        {
            ++counters_.refused_services;
            return refuse(error::unsupported_service);
        }
        return authenticate(session, *authentication);
    }

    expected_size_t server_session_table::seal_locked(const datagram_transport* const connection, const std::uint16_t session_id,
                                                      const cspan_uint8_t plain_frame, const span_uint8_t destination,
                                                      const bool authenticated_only) noexcept
    {
        auto* const session = find(session_id);
        if ((session == nullptr) || (session->connection != connection) || (session->next_sequence > max_sequence) ||
            (authenticated_only && (session->user == nullptr)))
            return refuse(error::secure_session_closed);
        const wrapper_fields fields {.session_id = session_id,
                                     .sequence = encode_sequence(session->next_sequence),
                                     .serial_number = configuration_->serial_number,
                                     .message_tag = tunnelling_message_tag};
        const auto sealed = seal_wrapper(destination, session->session_key, fields, plain_frame);
        if (sealed.has_value())
            ++session->next_sequence;
        return sealed;
    }

    expected_size_t server_session_table::seal(const datagram_transport* const connection, const std::uint16_t session_id,
                                               const cspan_uint8_t plain_frame, const span_uint8_t destination) noexcept
    {
        const std::lock_guard lock {mutex_};
        return seal_locked(connection, session_id, plain_frame, destination, true);
    }

    expected_size_t server_session_table::seal_status(const datagram_transport* const connection, const std::uint16_t session_id,
                                                      const session_status status, const span_uint8_t destination) noexcept
    {
        std::array<std::uint8_t, session_status_size> plain {};
        if (const auto encoded = encode_session_status_packet(plain, {status}); !encoded.has_value())
            return std::unexpected(encoded.error());
        const std::lock_guard lock {mutex_};
        return seal_locked(connection, session_id, plain, destination, false);
    }

    std::span<const individual_address> server_session_table::tunnel_addresses(const std::uint16_t session_id) const noexcept
    {
        const std::lock_guard lock {mutex_};
        const auto* const session = find(session_id);
        if ((session == nullptr) || (session->user == nullptr))
            return {};
        return session->user->tunnel_addresses;
    }

    bool server_session_table::alive(const std::uint16_t session_id) const noexcept
    {
        const std::lock_guard lock {mutex_};
        return find(session_id) != nullptr;
    }

    bool server_session_table::authenticated(const std::uint16_t session_id) const noexcept
    {
        const std::lock_guard lock {mutex_};
        const auto* const session = find(session_id);
        return (session != nullptr) && (session->user != nullptr);
    }

    bool server_session_table::has_sessions(const datagram_transport* const connection) const noexcept
    {
        const std::lock_guard lock {mutex_};
        return std::ranges::any_of(sessions_, [connection](const entry& value) noexcept { return value.connection == connection; });
    }

    void server_session_table::erase(const std::uint16_t session_id) noexcept
    {
        std::erase_if(sessions_, [session_id](const entry& value) noexcept { return value.session_id == session_id; });
    }

    void server_session_table::close(const std::uint16_t session_id) noexcept
    {
        const std::lock_guard lock {mutex_};
        erase(session_id);
    }

    std::size_t server_session_table::close_connection(const datagram_transport* const connection) noexcept
    {
        const std::lock_guard lock {mutex_};
        return std::erase_if(sessions_, [connection](const entry& value) noexcept { return value.connection == connection; });
    }

    std::size_t server_session_table::reap(const std::uint64_t now_ms) noexcept
    {
        const std::lock_guard lock {mutex_};
        const auto expired = [this, now_ms](const entry& value) noexcept
        {
            const auto unauthenticated_too_long =
                (value.user == nullptr) && (now_ms >= (value.created_ms + configuration_->unauthenticated_lifetime_ms));
            return unauthenticated_too_long || (now_ms >= (value.last_received_ms + session_timeout_ms));
        };
        const auto ended = std::erase_if(sessions_, expired);
        counters_.sessions_timed_out += ended;
        return ended;
    }

    void server_session_table::note_unencrypted() noexcept
    {
        const std::lock_guard lock {mutex_};
        ++counters_.unencrypted_refused;
    }

    std::size_t server_session_table::sessions() const noexcept
    {
        const std::lock_guard lock {mutex_};
        return sessions_.size();
    }

    statistics server_session_table::counters() const noexcept
    {
        const std::lock_guard lock {mutex_};
        return counters_;
    }
}
