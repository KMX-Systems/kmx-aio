/// @file kmx/aio/knx/server_secure.cpp
/// @brief The KNX IP Secure half of the tunnelling server: the sessions on each connection, and the channels opened in them.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/server.hpp>

#include <kmx/aio/knx/error.hpp>
#include <kmx/aio/knx/secure/server_session_table.hpp>
#include <kmx/aio/knx/secure/session_link.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <variant>
#include <vector>

namespace kmx::aio::knx
{
    /// @brief How long a secure connection waits for a frame before it looks at its sessions again, in milliseconds.
    static constexpr std::uint32_t secure_recheck_ms = 1'000u;

    /// @brief How many sessions one connection may open in its lifetime.
    /// @details A session's link lasts as long as its connection, because the channels opened in the session answer
    ///          through it. A client opening session after session on one connection is cut off here, rather than
    ///          growing the server without bound.
    static constexpr std::size_t sessions_per_connection = 16u;

    /// @brief The monotonic millisecond stamp a stream transport's deadlines are expressed in.
    [[nodiscard]] static std::uint32_t connection_now_ms() noexcept
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    struct generic_server::secure_connection
    {
        /// @brief The connection served.
        datagram_transport& connection;
        /// @brief The link of every session opened on the connection, kept until the connection ends.
        std::vector<std::unique_ptr<secure::session_link>> links {};
        /// @brief When the connection last carried a session, or was taken on.
        std::uint64_t last_session_ms {};
        /// @brief The frame in hand, as it arrived.
        std::array<std::uint8_t, frame::max_datagram_size> wire {};
        /// @brief What the wrapper in hand carried.
        std::array<std::uint8_t, frame::max_datagram_size> plain {};
    };

    std::uint64_t generic_server::secure_now_ms() const noexcept
    {
        if (secure_clock_ms_ != nullptr)
            return secure_clock_ms_();
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    std::size_t generic_server::secure_sessions() const noexcept
    {
        return secured() ? sessions_->sessions() : 0u;
    }

    secure::statistics generic_server::secure_counters() const noexcept
    {
        return secured() ? sessions_->counters() : secure::statistics {};
    }

    bool generic_server::refuses_in_clear(const origin& from) const noexcept
    {
        if (!secured() || (from.session_id != 0u))
            return false;
        sessions_->note_unencrypted();
        return true;
    }

    std::vector<dib::block> generic_server::advertised_blocks() const noexcept(false)
    {
        // A secure server says which of its families need KNX Secure, unless its configuration says so already.
        auto blocks = config_.description_blocks;
        if (secured() && (dib::find_service_families(blocks, true) == nullptr))
            blocks.emplace_back(dib::supported_service_families {
                .secured = true, .families = {dib::service_family_entry {.family = dib::service_family::tunnelling, .version = 1u}}});
        return blocks;
    }

    std::expected<individual_address, connect_status> generic_server::permitted_address(
        const std::span<const individual_address> permitted, const std::optional<individual_address>& requested) const noexcept
    {
        const auto in_use = [this](const individual_address address) noexcept
        {
            return std::ranges::any_of(channels_, [address](const channel& value) noexcept
                                       { return value.active && (value.assigned_address == address); });
        };
        // An address asked for has to be one of the user's own, and free.
        if (requested.has_value() && (std::ranges::find(permitted, *requested) == permitted.end()))
            return std::unexpected(connect_status::authorisation_error);
        if (requested.has_value())
            return in_use(*requested) ?
                       std::expected<individual_address, connect_status> {std::unexpected(connect_status::connection_in_use)} :
                       std::expected<individual_address, connect_status> {*requested};

        const auto free =
            std::ranges::find_if(permitted, [&in_use](const individual_address address) noexcept { return !in_use(address); });
        if (free != permitted.end())
            return *free;
        return std::unexpected(permitted.empty() ? connect_status::authorisation_error : connect_status::no_more_unique_connections);
    }

    generic_server::connection_offer generic_server::offer_secure_channel(const origin& from,
                                                                          const std::optional<individual_address>& requested,
                                                                          const transport_peer& data_peer,
                                                                          const hpai& data_endpoint) noexcept
    {
        // Taken before the table lock, never under it: the addresses belong to the configuration, which outlives both.
        const auto permitted = sessions_->tunnel_addresses(from.session_id);
        const std::lock_guard lock {table_mutex_};
        const auto address = permitted_address(permitted, requested);
        if (!address.has_value())
            return connection_offer {.status = address.error()};
        const auto channel_id = allocate_channel(std::nullopt);
        if (!channel_id.has_value())
            return connection_offer {.status = channel_id.error()};

        connection_offer offer {*channel_id, connect_status::no_error, *address, true};
        channels_[offer.channel_id] = channel {.active = true,
                                               .transport = from.transport,
                                               .peer = from.peer,
                                               .data_peer = data_peer,
                                               .data_endpoint = data_endpoint,
                                               .assigned_address = offer.assigned,
                                               .session_id = from.session_id};
        observe_activity(channels_[offer.channel_id]);
        return offer;
    }

    void generic_server::release_orphaned_channels() noexcept
    {
        for (std::uint16_t id = 1u; id < channels_.size(); ++id)
        {
            const auto& value = channels_[id];
            if (value.active && (value.session_id != 0u) && !sessions_->alive(value.session_id))
                release_channel(static_cast<std::uint8_t>(id));
        }
    }

    void generic_server::end_secure_connection(secure_connection& state) noexcept
    {
        // The channels go first: nothing may be left to answer through a link that is about to go.
        with_table(
            [this, &state]() noexcept
            {
                for (const auto& link: state.links)
                    release_channels_of(*link);
            });
        static_cast<void>(sessions_->close_connection(&state.connection));
    }

    bool generic_server::connection_idle(secure_connection& state) const noexcept
    {
        // A connection is for sessions. One that has carried none for as long as a handshake may take is closed, so a
        // client cannot hold a connection open without ever opening a session on it.
        const auto now = secure_now_ms();
        if (sessions_->has_sessions(&state.connection))
            state.last_session_ms = now;
        return now >= (state.last_session_ms + config_.secure->unauthenticated_lifetime_ms);
    }

    task_returning_expected_void_t generic_server::serve_secure_connection(datagram_transport& connection,
                                                                           const server_event_handler& on_event) noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        secure_connection state {.connection = connection, .last_session_ms = secure_now_ms()};
        state.links.reserve(sessions_per_connection);
        connection_step_t step {true};
        while (step.has_value() && *step)
        {
            if (shutdown_ || stop_token.stop_requested())
                step = std::unexpected(make_error_code(error::shutdown));
            else
                step = co_await serve_secure_frame(state, on_event);
        }

        end_secure_connection(state);
        connection.close();
        if (!step.has_value())
            co_return std::unexpected(step.error());
        co_return expected_void_t {};
    }

    task<generic_server::connection_step_t> generic_server::serve_secure_frame(secure_connection& state,
                                                                               const server_event_handler& on_event) noexcept(false)
    {
        if (const auto active = poll(); !active.has_value())
            co_return std::unexpected(active.error());
        if (connection_idle(state))
            co_return connection_step_t {false};

        // The wait is bounded, so a connection whose client has gone silent still has its sessions looked at.
        transport_peer peer {};
        const span_byte_t buffer {reinterpret_cast<std::byte*>(state.wire.data()), state.wire.size()};
        const auto received = co_await state.connection.receive_until(buffer, peer, connection_now_ms() + secure_recheck_ms);
        if (!received && (received.error() == make_error_code(error::timeout)))
            co_return connection_step_t {true};
        if (!received && (received.error() == make_error_code(error::shutdown)))
            co_return connection_step_t {false};
        if (!received)
            co_return std::unexpected(received.error());
        if ((*received == 0u) || (*received > state.wire.size()))
            co_return std::unexpected(make_error_code(error::invalid_length));
        co_return co_await route_secure_frame(state, {state.wire.data(), *received}, peer, on_event);
    }

    task<generic_server::connection_step_t> generic_server::route_secure_frame(secure_connection& state, const cspan_uint8_t packet,
                                                                               const transport_peer& peer,
                                                                               const server_event_handler& on_event) noexcept(false)
    {
        const auto decoded = decode_datagram(packet);
        if (!decoded.has_value())
            co_return recoverable(decoded.error()) ? connection_step_t {true} : connection_step_t {std::unexpected(decoded.error())};
        if (const auto* const request = std::get_if<secure::session_request_frame>(&decoded->payload))
            co_return co_await answer_session_request(state, *request, peer);
        if (const auto* const wrapper = std::get_if<secure::secure_wrapper_frame>(&decoded->payload))
            co_return co_await accept_wrapper(state, *wrapper, peer, on_event);

        // Discovery is answered in the clear, and the rest of what arrives unencrypted is refused (P1).
        const auto event = co_await dispatch(*decoded, origin {&state.connection, peer});
        if (!event.has_value())
            co_return recoverable(event.error()) ? connection_step_t {true} : connection_step_t {std::unexpected(event.error())};
        co_return connection_step_t {true};
    }

    task<generic_server::connection_step_t> generic_server::answer_session_request(secure_connection& state,
                                                                                   const secure::session_request_frame& request,
                                                                                   const transport_peer& peer) noexcept(false)
    {
        if (state.links.size() >= sessions_per_connection)
            co_return connection_step_t {false};
        const auto response = sessions_->on_session_request(&state.connection, peer, request, secure_now_ms());
        if (!response.has_value())
            co_return co_await refuse_session(state.connection, peer);
        state.links.push_back(std::make_unique<secure::session_link>(state.connection, *sessions_, response->session_id));

        std::array<std::uint8_t, secure::session_response_size> packet {};
        if (const auto encoded = secure::encode_session_response_packet(packet, *response); !encoded.has_value())
            co_return std::unexpected(encoded.error());
        const auto sent = co_await send_datagram(packet, origin {&state.connection, peer});
        co_return sent.has_value() ? connection_step_t {true} : connection_step_t {std::unexpected(sent.error())};
    }

    task<generic_server::connection_step_t> generic_server::refuse_session(datagram_transport& connection,
                                                                           const transport_peer& peer) noexcept(false)
    {
        // No key exists to seal the answer under, so it goes in the clear, and says only that no session was opened. A
        // client learns at once that it was turned away, instead of waiting out its handshake.
        std::array<std::uint8_t, secure::session_status_size> packet {};
        if (const auto encoded = secure::encode_session_status_packet(packet, {secure::session_status::unauthenticated});
            !encoded.has_value())
            co_return std::unexpected(encoded.error());
        const auto sent = co_await send_datagram(packet, origin {&connection, peer});
        co_return sent.has_value() ? connection_step_t {true} : connection_step_t {std::unexpected(sent.error())};
    }

    task<generic_server::connection_step_t> generic_server::accept_wrapper(secure_connection& state,
                                                                           const secure::secure_wrapper_frame& wrapper,
                                                                           const transport_peer& peer,
                                                                           const server_event_handler& on_event) noexcept(false)
    {
        // A wrapper that does not authenticate, replays an older one, or carries what it may not is counted and read past.
        const auto opened = sessions_->open(&state.connection, peer, wrapper, state.plain, secure_now_ms());
        if (!opened.has_value())
            co_return connection_step_t {true};
        const auto found = std::ranges::find(state.links, opened->session_id, [](const auto& link) noexcept { return link->session_id(); });
        if (found == state.links.end())
            co_return connection_step_t {true};

        auto& link = **found;
        switch (opened->kind)
        {
            case secure::server_frame_kind::tunnel:
                co_return co_await serve_session_frame(link, {state.plain.data(), opened->size}, peer, on_event);
            case secure::server_frame_kind::authenticated:
                static_cast<void>(co_await link.send_status(secure::session_status::authentication_success));
                break;
            case secure::server_frame_kind::refused_authentication:
                co_return co_await refuse_authentication(link);
            case secure::server_frame_kind::closed:
                with_table([this, &link]() noexcept { release_channels_of(link); });
                break;
            case secure::server_frame_kind::keep_alive:
                break;
        }
        co_return connection_step_t {true};
    }

    task<generic_server::connection_step_t> generic_server::refuse_authentication(secure::session_link& link) noexcept(false)
    {
        // The client is told under the session it tried to authenticate, and the session goes: another attempt takes
        // another handshake, under new keys.
        static_cast<void>(co_await link.send_status(secure::session_status::authentication_failed));
        sessions_->close(link.session_id());
        co_return connection_step_t {true};
    }

    task<generic_server::connection_step_t> generic_server::serve_session_frame(secure::session_link& link, const cspan_uint8_t packet,
                                                                                const transport_peer& peer,
                                                                                const server_event_handler& on_event) noexcept(false)
    {
        // A session's frames are answered through its link, and a channel opened by one belongs to that session alone.
        auto event = co_await handle_received(packet, origin {&link, peer, link.session_id()});
        if (!event.has_value())
            co_return recoverable(event.error()) ? connection_step_t {true} : connection_step_t {std::unexpected(event.error())};
        if (on_event && !event->cemi_bytes.empty())
            co_await on_event(std::move(*event));
        co_return connection_step_t {true};
    }
}
