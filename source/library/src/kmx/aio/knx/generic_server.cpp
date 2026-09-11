/// @file src/kmx/aio/knx/generic_server.cpp
/// @brief The compiled body of the executor-neutral KNXnet/IP tunnelling server, its KNX IP Secure half included.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/generic_server.hpp>
#ifndef PCH
    #include <kmx/aio/knx/cemi.hpp>
    #include <kmx/aio/knx/dib/supported_service_families.hpp>
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/secure/entropy.hpp>
    #include <kmx/aio/knx/secure/server_session_table.hpp>
    #include <kmx/aio/knx/secure/session_link.hpp>
    #include <kmx/aio/promise_base.hpp>

    #include <algorithm>
    #include <array>
    #include <chrono>
    #include <cstring>
    #include <memory>
    #include <optional>
    #include <variant>
    #include <vector>
    #include <netinet/in.h>
#endif

namespace kmx::aio::knx
{
    /// @brief The HPAI host protocol of KNXnet/IP over UDP.
    static constexpr std::uint8_t udp_host_protocol = 0x01u;
    /// @brief The HPAI host protocol of KNXnet/IP over TCP.
    static constexpr std::uint8_t tcp_host_protocol = 0x02u;
    /// @brief The HPAI a server names over TCP: the connection itself, with no address and no port.
    static constexpr hpai tcp_hpai {ipv4_endpoint {}, tcp_host_protocol};

    // A zero port is only unusable when the rest of the HPAI is not zero too. All-zero is the route-back
    // form: a client behind NAT saying "answer wherever this datagram came from", which is how most clients
    // on a routed network connect. Rejecting it turned the common case into invalid_configuration. Over TCP
    // both endpoints are the connection the request arrived on, so both have to be the TCP HPAI.
    [[nodiscard]] static expected_void_t validate_connect_request(const hpai& control, const hpai& data, const bool stream) noexcept
    {
        const auto protocol = stream ? tcp_host_protocol : udp_host_protocol;
        if ((control.protocol != protocol) || (data.protocol != protocol))
            return std::unexpected(make_error_code(error::unsupported_hpai));
        if (stream)
            return {};
        if ((control.endpoint.port == 0u) && !route_back(control))
            return std::unexpected(make_error_code(error::invalid_configuration));
        if ((data.endpoint.port == 0u) && !route_back(data))
            return std::unexpected(make_error_code(error::invalid_configuration));
        return {};
    }

    // A tunnel over TCP is asked for with the IPv4 TCP HPAI, so an IPv6 request on a connection is refused.
    [[nodiscard]] static expected_void_t validate_connect_request(const ipv6_hpai& control, const ipv6_hpai& data,
                                                                  const bool stream) noexcept
    {
        if (stream || (control.protocol != udp_host_protocol) || (data.protocol != udp_host_protocol))
            return std::unexpected(make_error_code(error::unsupported_hpai));
        if ((control.endpoint.port == 0u) && !route_back(control))
            return std::unexpected(make_error_code(error::invalid_configuration));
        if ((data.endpoint.port == 0u) && !route_back(data))
            return std::unexpected(make_error_code(error::invalid_configuration));
        return {};
    }

    [[nodiscard]] static transport_peer make_data_peer(const transport_peer& control_peer, const hpai& endpoint) noexcept
    {
        auto result = control_peer;
        // A route-back data endpoint names no address of its own, so the control peer - the source of the
        // CONNECT_REQUEST - is where the tunnelling traffic goes.
        if (route_back(endpoint))
            return result;
        if (result.address.ss_family == AF_INET)
        {
            auto& address = reinterpret_cast<sockaddr_in&>(result.address);
            std::memcpy(&address.sin_addr.s_addr, endpoint.endpoint.address.data(), endpoint.endpoint.address.size());
            address.sin_port = htons(endpoint.endpoint.port);
            result.length = sizeof(sockaddr_in);
        }

        return result;
    }

    [[nodiscard]] static transport_peer make_data_peer(const transport_peer& control_peer, const ipv6_hpai& endpoint) noexcept
    {
        auto result = control_peer;
        if (route_back(endpoint))
            return result;
        if (result.address.ss_family == AF_INET6)
        {
            auto& address = reinterpret_cast<sockaddr_in6&>(result.address);
            std::memcpy(&address.sin6_addr, endpoint.endpoint.address.data(), endpoint.endpoint.address.size());
            address.sin6_port = htons(endpoint.endpoint.port);
            result.length = sizeof(sockaddr_in6);
        }

        return result;
    }

    bool generic_server::recoverable(const std::error_code failure) noexcept
    {
        // A frame a secure server takes only inside a session, and a send in a session that has just ended, concern that
        // one frame as well.
        return (failure == make_error_code(error::timeout)) || (failure == make_error_code(error::send_queue_full)) ||
               (failure == make_error_code(error::unsupported_connection_type)) ||
               (failure == make_error_code(error::unsupported_service)) || (failure == make_error_code(error::sequence_error)) ||
               (failure == make_error_code(error::malformed_frame)) || (failure == make_error_code(error::invalid_configuration)) ||
               (failure == make_error_code(error::unsupported_hpai)) || (failure == make_error_code(error::invalid_length)) ||
               (failure == make_error_code(error::secure_frame_required)) || (failure == make_error_code(error::secure_session_closed));
    }

    generic_server::generic_server(datagram_transport& transport, const server_config config, const server_options options) noexcept(false):
        generic_server(config, options)
    {
        transport_ = &transport;
    }

    generic_server::generic_server(const server_config config, const server_options options) noexcept(false):
        config_(config),
        clock_now_(options.clock_now),
        secure_clock_ms_(options.secure_clock_ms)
    {
        if (config_.max_channels == 0u)
            config_.max_channels = 1u;
        if (config_.secure != nullptr)
            sessions_ = std::make_unique<secure::server_session_table>(
                config_.secure, (options.entropy != nullptr) ? *options.entropy : secure::system_entropy());
    }

    generic_server::~generic_server() noexcept = default;

    std::uint32_t generic_server::now_ms() const noexcept
    {
        if (clock_now_ != nullptr)
            return clock_now_();
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    void generic_server::observe_activity(channel& value) noexcept
    {
        value.last_activity_ms = now_ms();
    }

    void generic_server::note_activity(const std::uint8_t channel_id) noexcept
    {
        const std::lock_guard lock {table_mutex_};
        if (channel_open(channel_id))
            observe_activity(channels_[channel_id]);
    }

    bool generic_server::channel_open(const std::uint8_t channel_id) const noexcept
    {
        return (channel_id != 0u) && channels_[channel_id].active;
    }

    bool generic_server::channel_active(const std::uint8_t channel_id) const noexcept
    {
        return with_table([this, channel_id]() noexcept { return channel_open(channel_id); });
    }

    std::uint8_t generic_server::active_channels() const noexcept
    {
        const std::lock_guard lock {table_mutex_};
        std::uint8_t count {};
        for (std::uint16_t id = 1u; id < channels_.size(); ++id)
            if (channels_[id].active)
                ++count;
        return count;
    }

    std::expected<std::uint8_t, connect_status> generic_server::allocate_channel(
        const std::optional<individual_address>& requested) const noexcept
    {
        const auto limit = std::min<std::uint16_t>(config_.max_channels, 255u);
        // A channel and the address it is given are one slot, so an address asked for in the extended CRI picks the
        // channel as well: it has to be one this server hands out, and not already in use.
        if (requested.has_value())
        {
            const auto slot =
                static_cast<std::int32_t>(requested->value()) - static_cast<std::int32_t>(config_.first_assigned_address.value()) + 1;
            if ((slot < 1) || (slot > static_cast<std::int32_t>(limit)))
                return std::unexpected(connect_status::no_tunnelling_address);
            if (channels_[static_cast<std::size_t>(slot)].active)
                return std::unexpected(connect_status::connection_in_use);
            return static_cast<std::uint8_t>(slot);
        }

        for (std::uint16_t id = 1u; id <= limit; ++id)
            if (!channels_[id].active)
                return static_cast<std::uint8_t>(id);
        return std::unexpected(connect_status::no_more_connections);
    }

    individual_address generic_server::assigned_address(const std::uint8_t channel_id) const noexcept
    {
        const auto base = config_.first_assigned_address.value();
        return individual_address {static_cast<std::uint16_t>(base + channel_id - 1u)};
    }

    void generic_server::release_channel(const std::uint8_t channel_id) noexcept
    {
        if (channel_id != 0u)
            channels_[channel_id] = channel {};
    }

    void generic_server::release_channels_of(const datagram_transport& connection) noexcept
    {
        for (std::uint16_t id = 1u; id < channels_.size(); ++id)
            if (channels_[id].transport == &connection)
                release_channel(static_cast<std::uint8_t>(id));
    }

    void generic_server::release_all_channels() noexcept
    {
        for (std::uint16_t id = 1u; id < channels_.size(); ++id)
            release_channel(static_cast<std::uint8_t>(id));
    }

    bool generic_server::peer_matches(const channel& value, const origin& from, const bool data_endpoint) const noexcept
    {
        // A channel is held over the transport it was opened on: the same address on another connection is another
        // client.
        const auto& expected_peer = data_endpoint ? value.data_peer : value.peer;
        const auto& peer = from.peer;
        if (!value.active || (value.transport != from.transport) || (expected_peer.length == 0u) || (expected_peer.length != peer.length) ||
            (expected_peer.address.ss_family != peer.address.ss_family))
            return false;

        if ((peer.address.ss_family == AF_INET) && (peer.length >= sizeof(sockaddr_in)))
        {
            const auto& expected = reinterpret_cast<const sockaddr_in&>(expected_peer.address);
            const auto& actual = reinterpret_cast<const sockaddr_in&>(peer.address);
            return (expected.sin_port == actual.sin_port) && (expected.sin_addr.s_addr == actual.sin_addr.s_addr);
        }

        if ((peer.address.ss_family == AF_INET6) && (peer.length >= sizeof(sockaddr_in6)))
        {
            const auto& expected = reinterpret_cast<const sockaddr_in6&>(expected_peer.address);
            const auto& actual = reinterpret_cast<const sockaddr_in6&>(peer.address);
            return (expected.sin6_port == actual.sin6_port) && (expected.sin6_scope_id == actual.sin6_scope_id) &&
                   (std::memcmp(&expected.sin6_addr, &actual.sin6_addr, sizeof(expected.sin6_addr)) == 0);
        }

        return std::memcmp(&expected_peer.address, &peer.address, peer.length) == 0;
    }

    bool generic_server::holds_channel(const std::uint8_t channel_id, const origin& from, const bool data_endpoint) const noexcept
    {
        return channel_open(channel_id) && peer_matches(channels_[channel_id], from, data_endpoint);
    }

    task_returning_expected_void_t generic_server::send_datagram(const cspan_uint8_t packet, const origin& to) noexcept(false)
    {
        if (to.transport == nullptr)
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        const auto* bytes = reinterpret_cast<const std::byte*>(packet.data());
        const auto sent = co_await to.transport->send(cspan_byte_t {bytes, packet.size()},
                                                      reinterpret_cast<const sockaddr*>(&to.peer.address), to.peer.length);
        if (!sent)
            co_return std::unexpected(sent.error());
        if (*sent != packet.size())
            co_return std::unexpected(make_error_code(error::connection_failed));
        co_return expected_void_t {};
    }

    bool generic_server::answers_discovery() const noexcept
    {
        return config_.control_endpoint.protocol == udp_host_protocol;
    }

    /// @brief Builds the peer a discovery answer is sent to from the HPAI the request carried.
    /// @param endpoint The endpoint the requester named.
    /// @param source The address the request arrived from.
    /// @param stream Whether the request arrived on a connection.
    /// @return The endpoint to answer, or nothing when the HPAI is unusable.
    /// @details A zero address and port is the route-back HPAI: the requester is behind NAT and asks to be
    ///          answered wherever the datagram came from. Over TCP the requester names the TCP HPAI, and the
    ///          answer goes back along the connection.
    [[nodiscard]] static std::optional<transport_peer> discovery_reply_peer(const hpai& endpoint, const transport_peer& source,
                                                                            const bool stream) noexcept
    {
        if (stream)
            return (endpoint.protocol == tcp_host_protocol) ? std::optional<transport_peer> {source} : std::nullopt;
        if (endpoint.protocol != udp_host_protocol)
            return std::nullopt;
        if (route_back(endpoint))
            return source;

        transport_peer result {};
        auto& address = reinterpret_cast<sockaddr_in&>(result.address);
        address.sin_family = AF_INET;
        std::memcpy(&address.sin_addr.s_addr, endpoint.endpoint.address.data(), endpoint.endpoint.address.size());
        address.sin_port = htons(endpoint.endpoint.port);
        result.length = sizeof(sockaddr_in);
        return result;
    }

    // A discovery request never becomes an event: it is answered and the caller goes on serving. The
    // timeout error is how @ref serve_once already reported "handled, nothing to hand back", so the three
    // answers below report it themselves rather than having the dispatcher translate for them.
    server_event_task_t generic_server::answer_search(const discovery::search_request_frame& request, const origin& from) noexcept(false)
    {
        if (!answers_discovery())
            co_return std::unexpected(make_error_code(error::timeout));

        const auto destination = discovery_reply_peer(request.discovery_endpoint, from.peer, from.transport->stream_oriented());
        if (!destination.has_value())
            co_return std::unexpected(make_error_code(error::unsupported_hpai));
        if (const auto built = with_table([this]() { return build_discovery_packets(); }); !built.has_value())
            co_return std::unexpected(built.error());
        if (const auto sent = co_await send_datagram(search_response_packet_, origin {from.transport, *destination}); !sent)
            co_return std::unexpected(sent.error());

        co_return std::unexpected(make_error_code(error::timeout));
    }

    server_event_task_t generic_server::answer_description(const discovery::description_request_frame& request,
                                                           const origin& from) noexcept(false)
    {
        if (!answers_discovery())
            co_return std::unexpected(make_error_code(error::timeout));

        const auto destination = discovery_reply_peer(request.control_endpoint, from.peer, from.transport->stream_oriented());
        if (!destination.has_value())
            co_return std::unexpected(make_error_code(error::unsupported_hpai));
        if (const auto built = with_table([this]() { return build_discovery_packets(); }); !built.has_value())
            co_return std::unexpected(built.error());
        if (const auto sent = co_await send_datagram(description_response_packet_, origin {from.transport, *destination}); !sent)
            co_return std::unexpected(sent.error());

        co_return std::unexpected(make_error_code(error::timeout));
    }

    bool generic_server::serves_service_family(const dib::service_family family, const std::uint8_t version) const noexcept
    {
        const auto* families = dib::find_service_families(config_.description_blocks);
        return (families != nullptr) && families->contains(family, version);
    }

    expected_void_t generic_server::build_discovery_packets() noexcept(false)
    {
        // Encoded on the first discovery answer and kept. The description is a function of the
        // configuration alone, and the configuration is fixed once the server is constructed, so building
        // it per request would repeat identical work every time a multicast search reaches this server -
        // which, on a subnet ETS is scanning, is every server at once.
        if (discovery_packets_built_)
            return {};

        // The verbatim tail is checked here rather than trusted: a malformed run of blocks would be a
        // datagram no peer can walk, and failing locally names the configuration that caused it.
        if (!config_.device_info_blocks.empty() && !dib::valid_blocks(config_.device_info_blocks))
            return std::unexpected(make_error_code(error::invalid_configuration));

        const auto blocks = advertised_blocks();
        description_blocks_.assign(dib::encoded_size(blocks) + config_.device_info_blocks.size(), 0u);
        const auto written = dib::encode_all(description_blocks_, blocks);
        if (!written.has_value())
            return std::unexpected(written.error());

        std::copy(config_.device_info_blocks.begin(), config_.device_info_blocks.end(), description_blocks_.begin() + *written);

        const auto endpoint_size = frame::communication_header_size + connection::hpai_size + description_blocks_.size();
        search_response_packet_.assign(endpoint_size, 0u);
        if (const auto encoded = discovery::encode_search_response_packet(
                search_response_packet_, discovery::search_response_frame {config_.control_endpoint, description_blocks_});
            !encoded.has_value())
            return std::unexpected(encoded.error());

        description_response_packet_.assign(frame::communication_header_size + description_blocks_.size(), 0u);
        if (const auto encoded = discovery::encode_description_response_packet(description_response_packet_,
                                                                               discovery::description_response_frame {description_blocks_});
            !encoded.has_value())
            return std::unexpected(encoded.error());

        discovery_packets_built_ = true;
        return {};
    }

    bool generic_server::matches_search_parameters(const discovery::extended_search_request_frame& request) const noexcept
    {
        for (const auto& parameter: request.parameters)
        {
            bool matched {};
            switch (parameter.type)
            {
                case discovery::search_parameter_type::programming_mode:
                    matched = config_.programming_mode;
                    break;
                case discovery::search_parameter_type::mac_address:
                    matched = (parameter.data.size() == config_.mac_address.size()) &&
                              std::equal(parameter.data.begin(), parameter.data.end(), config_.mac_address.begin());
                    break;
                case discovery::search_parameter_type::service:
                    // Answered from the very list this server advertises, so a search that selects by
                    // service cannot get a different answer than a description of the same server would.
                    matched = (parameter.data.size() == 2u) && dib::known_service_family(parameter.data[0u]) &&
                              serves_service_family(static_cast<dib::service_family>(parameter.data[0u]), parameter.data[1u]);
                    break;
                case discovery::search_parameter_type::request_dibs:
                    // Not a selection criterion: it asks which blocks to report. This server reports the
                    // set it was configured with either way.
                    matched = true;
                    break;
            }

            // A mandatory parameter this server cannot satisfy means silence, which is the point of the
            // flag: the searcher wants only servers that match, not an answer explaining that one does not.
            if (!matched && parameter.mandatory)
                return false;
        }

        return true;
    }

    byte_buffer_t generic_server::requested_description_blocks(const discovery::extended_search_request_frame& request) const
    {
        byte_buffer_t requested_types {};
        for (const auto& parameter: request.parameters)
            if (parameter.type == discovery::search_parameter_type::request_dibs)
                requested_types.insert(requested_types.end(), parameter.data.begin(), parameter.data.end());
        if (requested_types.empty())
            return description_blocks_;

        byte_buffer_t result {};
        for (std::size_t offset {}; offset < description_blocks_.size();)
        {
            const auto block_size = description_blocks_[offset];
            if (std::ranges::find(requested_types, description_blocks_[offset + 1u]) != requested_types.end())
                result.insert(result.end(), description_blocks_.begin() + offset, description_blocks_.begin() + offset + block_size);
            offset += block_size;
        }

        return result;
    }

    server_event_task_t generic_server::answer_extended_search(const discovery::extended_search_request_frame& request,
                                                               const origin& from) noexcept(false)
    {
        if (!answers_discovery() || !matches_search_parameters(request))
            co_return std::unexpected(make_error_code(error::timeout));

        const auto destination = discovery_reply_peer(request.discovery_endpoint, from.peer, from.transport->stream_oriented());
        if (!destination.has_value())
            co_return std::unexpected(make_error_code(error::unsupported_hpai));
        if (const auto built = with_table([this]() { return build_discovery_packets(); }); !built.has_value())
            co_return std::unexpected(built.error());
        const auto description = requested_description_blocks(request);
        byte_buffer_t packet(frame::communication_header_size + connection::hpai_size + description.size(), 0u);
        if (const auto encoded = discovery::encode_extended_search_response_packet(
                packet, discovery::extended_search_response_frame {config_.control_endpoint, description});
            !encoded.has_value())
            co_return std::unexpected(encoded.error());
        if (const auto sent = co_await send_datagram(packet, origin {from.transport, *destination}); !sent)
            co_return std::unexpected(sent.error());

        co_return std::unexpected(make_error_code(error::timeout));
    }

    generic_server::connection_offer generic_server::offer_channel(const origin& from, const std::optional<individual_address>& requested,
                                                                   const transport_peer& data_peer, const hpai& data_endpoint) noexcept
    {
        const std::lock_guard lock {table_mutex_};
        const auto channel_id = allocate_channel(requested);
        if (!channel_id.has_value())
            return connection_offer {.status = channel_id.error()};

        connection_offer offer {*channel_id, connect_status::no_error, assigned_address(*channel_id), true};
        channels_[offer.channel_id] = channel {
            .active = true,
            .transport = from.transport,
            .peer = from.peer,
            .data_peer = data_peer,
            .data_endpoint = data_endpoint,
            .assigned_address = offer.assigned,
        };
        observe_activity(channels_[offer.channel_id]);
        return offer;
    }

    server_event_task_t generic_server::refuse_ipv6_connect(const ipv6_hpai& data_endpoint, const connect_status status,
                                                            const std::error_code reason, const origin& from) noexcept(false)
    {
        auto response_endpoint = data_endpoint;
        response_endpoint.protocol = udp_host_protocol;
        std::array<std::uint8_t, frame::communication_header_size + connection::ipv6_connect_response_body_size> response {};
        if (const auto encoded =
                connection::encode_ipv6_connect_response_packet(response, ipv6_connect_response_frame {0u, status, response_endpoint, {}});
            !encoded.has_value())
            co_return std::unexpected(encoded.error());
        if (const auto sent = co_await send_datagram(response, from); !sent)
            co_return std::unexpected(sent.error());
        co_return std::unexpected(reason);
    }

    server_event_task_t generic_server::open_ipv6_channel(const ipv6_connect_request_frame& request, const std::uint16_t service_type,
                                                          const origin& from) noexcept(false)
    {
        if (service_type != connection::connect_request_service)
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        if (refuses_in_clear(from))
            co_return co_await refuse_ipv6_connect(request.data_endpoint, connect_status::connection_type,
                                                   make_error_code(error::secure_frame_required), from);
        if (const auto valid = validate_connect_request(request.control_endpoint, request.data_endpoint, from.transport->stream_oriented());
            !valid.has_value())
            co_return co_await refuse_ipv6_connect(request.data_endpoint, connect_status::host_protocol_type, valid.error(), from);

        const auto offer = offer_channel(from, std::nullopt, make_data_peer(from.peer, request.data_endpoint), hpai {});
        std::array<std::uint8_t, frame::communication_header_size + connection::ipv6_connect_response_body_size> response {};
        if (const auto encoded = connection::encode_ipv6_connect_response_packet(
                response, ipv6_connect_response_frame {offer.channel_id, offer.status, request.data_endpoint, offer.assigned});
            !encoded.has_value())
            co_return std::unexpected(encoded.error());
        if (const auto sent = co_await send_datagram(response, from); !sent)
            co_return std::unexpected(sent.error());

        // The refusal is reported only after it has been sent: the client is owed the CONNECT_RESPONSE
        // that names the reason, and dropping out earlier would leave it waiting for one.
        if (!offer.accepted)
            co_return std::unexpected(make_error_code(error::send_queue_full));
        co_return server_event {offer.channel_id, {}};
    }

    server_event_task_t generic_server::refuse_connect(const hpai& data_endpoint, const connect_status status, const std::error_code reason,
                                                       const origin& from) noexcept(false)
    {
        // The refusal is reported only after it has been sent: the client is owed the CONNECT_RESPONSE that
        // names the reason, and dropping out earlier would leave it waiting for one. Over TCP the response names
        // the connection, as every response on it does.
        auto response_endpoint = data_endpoint;
        response_endpoint.protocol = udp_host_protocol;
        if (from.transport->stream_oriented())
            response_endpoint = tcp_hpai;
        std::array<std::uint8_t, frame::communication_header_size + connection::connect_response_body_size> response {};
        if (const auto encoded =
                connection::encode_connect_response_packet(response, connect_response_frame {0u, status, response_endpoint, {}});
            !encoded.has_value())
            co_return std::unexpected(encoded.error());
        if (const auto sent = co_await send_datagram(response, from); !sent)
            co_return std::unexpected(sent.error());
        co_return std::unexpected(reason);
    }

    server_event_task_t generic_server::answer_connect(const connection_offer& offer, const hpai& data_endpoint,
                                                       const origin& from) noexcept(false)
    {
        std::array<std::uint8_t, frame::communication_header_size + connection::connect_response_body_size> response {};
        if (const auto encoded = connection::encode_connect_response_packet(
                response, connect_response_frame {offer.channel_id, offer.status, data_endpoint, offer.assigned});
            !encoded.has_value())
            co_return std::unexpected(encoded.error());
        if (const auto sent = co_await send_datagram(response, from); !sent)
            co_return std::unexpected(sent.error());

        // The refusal is reported only after it has been sent: the client is owed the CONNECT_RESPONSE
        // that names the reason, and dropping out earlier would leave it waiting for one.
        if (offer.status == connect_status::no_more_connections)
            co_return std::unexpected(make_error_code(error::send_queue_full));
        if (!offer.accepted)
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        co_return server_event {offer.channel_id, {}};
    }

    server_event_task_t generic_server::open_channel(const connect_request_frame& request, const std::uint16_t service_type,
                                                     const origin& from) noexcept(false)
    {
        if (service_type != connection::connect_request_service)
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        // A secure server tunnels inside sessions alone. An unencrypted request is refused before anything is allocated, so
        // no plain channel can open beside the secure ones (P1).
        if (refuses_in_clear(from))
            co_return co_await refuse_connect(request.data_endpoint, connect_status::connection_type,
                                              make_error_code(error::secure_frame_required), from);
        const auto stream = from.transport->stream_oriented();
        if (const auto valid = validate_connect_request(request.control_endpoint, request.data_endpoint, stream); !valid.has_value())
            co_return co_await refuse_connect(request.data_endpoint, connect_status::host_protocol_type, valid.error(), from);
        // The codec can carry every KNX layer, but this server serves only the link layer: a bus monitor
        // or raw tunnel delivers L_Busmon and L_Raw frames, which the cEMI layer here does not decode.
        // Refusing the connection with E_TUNNELLING_LAYER is honest; accepting it and then rejecting every
        // frame is not.
        if (request.knx_layer != connection::tunnel_link_layer)
            co_return co_await refuse_connect(request.data_endpoint, connect_status::tunnelling_layer,
                                              make_error_code(error::unsupported_connection_type), from);

        // Over TCP the data channel is the connection the request arrived on, and the response names it so.
        const auto data_endpoint = stream ? tcp_hpai : request.data_endpoint;
        const auto data_peer = stream ? from.peer : make_data_peer(from.peer, request.data_endpoint);
        const auto offer = (from.session_id != 0u) ? offer_secure_channel(from, request.requested_address, data_peer, data_endpoint) :
                                                     offer_channel(from, request.requested_address, data_peer, data_endpoint);
        co_return co_await answer_connect(offer, data_endpoint, from);
    }

    server_event_task_t generic_server::refuse_unknown_channel(const std::uint16_t response_service, const std::uint8_t channel_id,
                                                               const origin& from) noexcept(false)
    {
        // A client asking after a channel the server does not hold - reclaimed for inactivity, or never
        // allocated - is told so with E_CONNECTION_ID, which makes it reconnect at once instead of after three
        // unanswered heartbeats. The table is not touched, so a peer naming someone else's channel learns only
        // that it does not hold it.
        std::array<std::uint8_t, frame::communication_header_size + 2u> response {};
        const auto encoded = (response_service == connection::connectionstate_response_service) ?
                                 connection::encode_connectionstate_response_packet(
                                     response, connectionstate_response_frame {channel_id, connect_status::connection_id}) :
                                 connection::encode_disconnect_response_packet(
                                     response, disconnect_response_frame {channel_id, connect_status::connection_id});
        if (!encoded.has_value())
            co_return std::unexpected(encoded.error());
        if (const auto sent = co_await send_datagram(response, from); !sent)
            co_return std::unexpected(sent.error());
        co_return std::unexpected(make_error_code(error::sequence_error));
    }

    server_event_task_t generic_server::answer_connectionstate(const connectionstate_request_frame& request,
                                                               const origin& from) noexcept(false)
    {
        if (!with_table([this, &request, &from]() noexcept { return holds_channel(request.channel_id, from); }))
            co_return co_await refuse_unknown_channel(connection::connectionstate_response_service, request.channel_id, from);

        std::array<std::uint8_t, frame::communication_header_size + 2u> response {};
        if (const auto encoded = connection::encode_connectionstate_response_packet(
                response, connectionstate_response_frame {request.channel_id, connect_status::no_error});
            !encoded.has_value())
            co_return std::unexpected(encoded.error());
        if (const auto sent = co_await send_datagram(response, from); !sent)
            co_return std::unexpected(sent.error());

        note_activity(request.channel_id);
        co_return server_event {request.channel_id, {}};
    }

    server_event_task_t generic_server::answer_disconnect(const disconnect_request_frame& request, const origin& from) noexcept(false)
    {
        if (!with_table([this, &request, &from]() noexcept { return holds_channel(request.channel_id, from); }))
            co_return co_await refuse_unknown_channel(connection::disconnect_response_service, request.channel_id, from);

        std::array<std::uint8_t, frame::communication_header_size + 2u> response {};
        if (const auto encoded = connection::encode_disconnect_response_packet(
                response, disconnect_response_frame {request.channel_id, connect_status::no_error});
            !encoded.has_value())
            co_return std::unexpected(encoded.error());
        if (const auto sent = co_await send_datagram(response, from); !sent)
            co_return std::unexpected(sent.error());

        with_table(
            [this, &request, &from]() noexcept
            {
                if (holds_channel(request.channel_id, from))
                    release_channel(request.channel_id);
            });
        co_return server_event {request.channel_id, {}};
    }

    task_returning_expected_void_t generic_server::send_tunnelling_ack(const std::uint8_t channel_id, const std::uint8_t sequence_number,
                                                                       const origin& to) noexcept(false)
    {
        // Over TCP nothing is acknowledged: the connection has already delivered the frame, once.
        if (to.transport->stream_oriented())
            co_return expected_void_t {};

        std::array<std::uint8_t, frame::communication_header_size + frame::tunnelling_ack_size> response {};
        if (const auto encoded = frame::encode_tunnelling_ack_packet(response, channel_id, sequence_number); !encoded.has_value())
            co_return std::unexpected(encoded.error());
        co_return co_await send_datagram(response, to);
    }

    generic_server::indication_verdict generic_server::classify_indication(const tunnelling_request_frame& request,
                                                                           const origin& from) noexcept
    {
        if (!holds_channel(request.channel_id, from, true))
            return indication_verdict::refused;
        // Over TCP the connection already delivers every frame once and in order, so there is nothing to check.
        if (from.transport->stream_oriented())
            return indication_verdict::fresh;

        auto& channel = channels_[request.channel_id];
        if (channel.incoming_sequence_valid && (request.sequence_number == channel.last_incoming_sequence))
            return indication_verdict::repeat;
        if (channel.incoming_sequence_valid && (request.sequence_number != channel.next_incoming_sequence))
            return indication_verdict::refused;

        channel.incoming_sequence_valid = true;
        channel.last_incoming_sequence = request.sequence_number;
        channel.next_incoming_sequence = static_cast<std::uint8_t>(request.sequence_number + 1u);
        return indication_verdict::fresh;
    }

    server_event_task_t generic_server::accept_tunnelled(const tunnelling_request_frame& request, const origin& from) noexcept(false)
    {
        const auto verdict = with_table([this, &request, &from]() noexcept { return classify_indication(request, from); });
        if (verdict == indication_verdict::refused)
            co_return std::unexpected(make_error_code(error::sequence_error));

        // A repeat of the frame just seen is acknowledged again and dropped: the client is retrying
        // because it missed the acknowledgement, not because it has something new to say.
        if (const auto sent = co_await send_tunnelling_ack(request.channel_id, request.sequence_number, from); !sent)
            co_return std::unexpected(sent.error());
        note_activity(request.channel_id);
        if (verdict == indication_verdict::repeat)
            co_return server_event {request.channel_id, {}};
        co_return server_event {request.channel_id, byte_buffer_t(request.cemi_bytes.begin(), request.cemi_bytes.end())};
    }

    server_event_task_t generic_server::dispatch(const datagram& value, const origin& from) noexcept(false)
    {
        // Discovery is connectionless and is answered before any channel bookkeeping: a server that never
        // replies to SEARCH cannot be found by ETS or by any other client, however well its tunnelling
        // works. Every answer goes to the endpoint the request names, not back to the multicast group.
        if (const auto* request = std::get_if<discovery::search_request_frame>(&value.payload))
            co_return co_await answer_search(*request, from);
        if (const auto* request = std::get_if<discovery::extended_search_request_frame>(&value.payload))
            co_return co_await answer_extended_search(*request, from);
        if (const auto* request = std::get_if<discovery::description_request_frame>(&value.payload))
            co_return co_await answer_description(*request, from);
        // Beyond discovery, a secure server takes nothing in the clear but a CONNECT_REQUEST, and that only to refuse it by
        // name (P1).
        if (!std::holds_alternative<connect_request_frame>(value.payload) &&
            !std::holds_alternative<ipv6_connect_request_frame>(value.payload) && refuses_in_clear(from))
            co_return std::unexpected(make_error_code(error::secure_frame_required));

        if (const auto* request = std::get_if<ipv6_connect_request_frame>(&value.payload))
            co_return co_await open_ipv6_channel(*request, value.service_type, from);
        if (const auto* request = std::get_if<connect_request_frame>(&value.payload))
            co_return co_await open_channel(*request, value.service_type, from);
        if (const auto* request = std::get_if<connectionstate_request_frame>(&value.payload))
            co_return co_await answer_connectionstate(*request, from);
        if (const auto* request = std::get_if<disconnect_request_frame>(&value.payload))
            co_return co_await answer_disconnect(*request, from);
        if (const auto* request = std::get_if<tunnelling_request_frame>(&value.payload))
            co_return co_await accept_tunnelled(*request, from);

        co_return std::unexpected(make_error_code(error::unsupported_service));
    }

    server_event_task_t generic_server::handle_received(const cspan_uint8_t packet, const origin& from) noexcept(false)
    {
        const auto decoded = decode_datagram(packet);
        if (!decoded.has_value())
            co_return std::unexpected(decoded.error());

        co_return co_await dispatch(decoded.value(), from);
    }

    server_event_task_t generic_server::serve_once() noexcept(false)
    {
        if (shutdown_)
            co_return std::unexpected(make_error_code(error::shutdown));
        if (transport_ == nullptr)
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        if (const auto active = poll(); !active.has_value())
            co_return std::unexpected(active.error());

        transport_peer peer {};
        auto* bytes = reinterpret_cast<std::byte*>(receive_buffer_.data());
        const auto received = co_await transport_->receive(span_byte_t {bytes, receive_buffer_.size()}, peer);
        if (!received)
            co_return std::unexpected(received.error());
        if ((received.value() == 0u) || (received.value() > receive_buffer_.size()))
            co_return std::unexpected(make_error_code(error::invalid_length));

        const origin from {transport_, peer};
        co_return co_await handle_received({receive_buffer_.data(), received.value()}, from);
    }

    task_returning_expected_void_t generic_server::serve() noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (shutdown_)
            co_return std::unexpected(make_error_code(error::shutdown));
        // Every failure serve_once reports without a transport is recoverable, so this loop would never end.
        if (transport_ == nullptr)
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        while (!shutdown_)
        {
            if (stop_token.stop_requested())
            {
                shutdown_ = true;
                co_return std::unexpected(make_error_code(error::shutdown));
            }

            const auto event = co_await serve_once();
            if (!event.has_value() && !recoverable(event.error()))
                co_return std::unexpected(event.error());
        }

        co_return expected_void_t {};
    }

    task_returning_expected_void_t generic_server::serve_connection(datagram_transport& connection,
                                                                    const server_event_handler on_event) noexcept(false)
    {
        if (secured())
            co_return co_await serve_secure_connection(connection, on_event);
        const auto stop_token = co_await get_stop_token;
        std::array<std::uint8_t, frame::max_datagram_size> buffer {};
        connection_step_t step {true};
        while (step.has_value() && *step)
            if (shutdown_ || stop_token.stop_requested())
                step = std::unexpected(make_error_code(error::shutdown));
            else
                step = co_await serve_connection_frame(connection, buffer, on_event);

        // A channel lasts no longer than the connection it was opened over, and the connection is finished with.
        with_table([this, &connection]() noexcept { release_channels_of(connection); });
        connection.close();
        if (!step.has_value())
            co_return std::unexpected(step.error());
        co_return expected_void_t {};
    }

    task<generic_server::connection_step_t> generic_server::serve_connection_frame(datagram_transport& connection,
                                                                                   const span_uint8_t buffer,
                                                                                   const server_event_handler& on_event) noexcept(false)
    {
        if (const auto active = poll(); !active.has_value())
            co_return std::unexpected(active.error());

        transport_peer peer {};
        const auto received = co_await connection.receive(span_byte_t {reinterpret_cast<std::byte*>(buffer.data()), buffer.size()}, peer);
        // The peer closing between frames is how a connection ordinarily ends. Any other failure of the stream ends it
        // too, and is reported.
        if (!received && (received.error() == make_error_code(error::shutdown)))
            co_return connection_step_t {false};
        if (!received)
            co_return std::unexpected(received.error());
        if ((received.value() == 0u) || (received.value() > buffer.size()))
            co_return std::unexpected(make_error_code(error::invalid_length));

        const origin from {&connection, peer};
        auto event = co_await handle_received({buffer.data(), received.value()}, from);
        if (!event.has_value())
            co_return recoverable(event.error()) ? connection_step_t {true} : connection_step_t {std::unexpected(event.error())};
        if (on_event && !event->cemi_bytes.empty())
            co_await on_event(std::move(*event));
        co_return connection_step_t {true};
    }

    std::optional<generic_server::outgoing> generic_server::claim_sequence(const std::uint8_t channel_id) noexcept
    {
        const std::lock_guard lock {table_mutex_};
        if (!channel_open(channel_id))
            return std::nullopt;
        auto& value = channels_[channel_id];
        return outgoing {origin {value.transport, value.data_peer}, value.sequence++};
    }

    task_returning_expected_void_t generic_server::send(const std::uint8_t channel_id, const cspan_uint8_t cemi_bytes) noexcept(false)
    {
        if (!channel_active(channel_id))
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        if (cemi_bytes.empty())
            co_return std::unexpected(make_error_code(error::malformed_frame));
        // The same bound the client's send path applies: a longer message encodes into a frame no cEMI
        // decoder can accept, so refusing it here is the difference between a local error and a peer that
        // drops the frame.
        if (cemi_bytes.size() > cemi::max_message_size)
            co_return std::unexpected(make_error_code(error::invalid_length));

        // The sequence number is taken and the destination read in one step; the send happens outside the table.
        const auto claimed = claim_sequence(channel_id);
        if (!claimed.has_value())
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        std::array<std::uint8_t, frame::max_datagram_size> buffer {};
        if (const auto encoded = frame::encode_tunnelling_request_packet(buffer, channel_id, claimed->sequence, cemi_bytes);
            !encoded.has_value())
            co_return std::unexpected(encoded.error());
        const auto size = frame::communication_header_size + frame::tunnelling_request_header_size + cemi_bytes.size();
        if (const auto sent = co_await send_datagram(cspan_uint8_t {buffer.data(), size}, claimed->destination); !sent)
            co_return std::unexpected(sent.error());
        note_activity(channel_id);
        co_return expected_void_t {};
    }

    expected_void_t generic_server::disconnect(const std::uint8_t channel_id) noexcept
    {
        const std::lock_guard lock {table_mutex_};
        if (!channel_open(channel_id))
            return std::unexpected(make_error_code(error::invalid_configuration));
        release_channel(channel_id);
        return {};
    }

    expected_void_t generic_server::shutdown() noexcept
    {
        shutdown_ = true;
        with_table([this]() noexcept { release_all_channels(); });
        return {};
    }

    expected_void_t generic_server::reset() noexcept
    {
        with_table([this]() noexcept { release_all_channels(); });
        shutdown_ = false;
        return {};
    }

    expected_void_t generic_server::poll() noexcept
    {
        if (shutdown_)
            return std::unexpected(make_error_code(error::shutdown));
        // Secure sessions time out on a clock of their own, and a channel lasts no longer than the session it was opened in.
        if (secured() && (sessions_->reap(secure_now_ms()) > 0u))
            with_table([this]() noexcept { release_orphaned_channels(); });
        if (config_.inactivity_timeout_ms == 0u)
            return {};

        const auto current = now_ms();
        const std::lock_guard lock {table_mutex_};
        for (std::uint16_t id = 1u; id < channels_.size(); ++id)
        {
            const auto channel_id = static_cast<std::uint8_t>(id);
            if (!channels_[channel_id].active)
                continue;
            if (static_cast<std::int32_t>(current - channels_[channel_id].last_activity_ms) >=
                static_cast<std::int32_t>(config_.inactivity_timeout_ms))
                release_channel(channel_id);
        }

        return {};
    }

    // The KNX IP Secure half of the server: the sessions on each connection, and the channels opened in them.

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
            if (shutdown_ || stop_token.stop_requested())
                step = std::unexpected(make_error_code(error::shutdown));
            else
                step = co_await serve_secure_frame(state, on_event);

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
        if (const auto* const wrapper = std::get_if<secure::wrapper_frame>(&decoded->payload))
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
        const auto response = sessions_->on_session_request({&state.connection, peer}, request, secure_now_ms());
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

    task<generic_server::connection_step_t> generic_server::accept_wrapper(secure_connection& state, const secure::wrapper_frame& wrapper,
                                                                           const transport_peer& peer,
                                                                           const server_event_handler& on_event) noexcept(false)
    {
        // A wrapper that does not authenticate, replays an older one, or carries what it may not is counted and read past.
        const auto opened = sessions_->open({&state.connection, peer}, wrapper, state.plain, secure_now_ms());
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
