/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/server.hpp>

#include <chrono>
#include <cstring>
#include <optional>

namespace kmx::aio::knx
{
    // A zero port is only unusable when the rest of the HPAI is not zero too. All-zero is the route-back
    // form: a client behind NAT saying "answer wherever this datagram came from", which is how most clients
    // on a routed network connect. Rejecting it turned the common case into invalid_configuration.
    [[nodiscard]] static expected_void_t validate_connect_request(const hpai& control, const hpai& data) noexcept
    {
        if ((control.protocol != 0x01u) || (data.protocol != 0x01u))
            return std::unexpected(make_error_code(error::unsupported_hpai));
        if ((control.endpoint.port == 0u) && !route_back(control))
            return std::unexpected(make_error_code(error::invalid_configuration));
        if ((data.endpoint.port == 0u) && !route_back(data))
            return std::unexpected(make_error_code(error::invalid_configuration));
        return {};
    }

    [[nodiscard]] static expected_void_t validate_connect_request(const ipv6_hpai& control, const ipv6_hpai& data) noexcept
    {
        if ((control.protocol != 0x01u) || (data.protocol != 0x01u))
            return std::unexpected(make_error_code(error::unsupported_hpai));
        if ((control.endpoint.port == 0u) && !route_back(control))
            return std::unexpected(make_error_code(error::invalid_configuration));
        if ((data.endpoint.port == 0u) && !route_back(data))
            return std::unexpected(make_error_code(error::invalid_configuration));
        return {};
    }

    [[nodiscard]] static transport_peer make_data_peer(
        const transport_peer& control_peer, const hpai& endpoint) noexcept
    {
        auto result = control_peer;
        // A route-back data endpoint names no address of its own, so the control peer - the source of the
        // CONNECT_REQUEST - is where the tunnelling traffic goes.
        if (route_back(endpoint))
            return result;
        if (result.address.ss_family == AF_INET)
        {
            auto& address = reinterpret_cast<sockaddr_in&>(result.address);
            std::memcpy(&address.sin_addr.s_addr, endpoint.endpoint.address.data(),
                        endpoint.endpoint.address.size());
            address.sin_port = htons(endpoint.endpoint.port);
            result.length = sizeof(sockaddr_in);
        }
        return result;
    }

    [[nodiscard]] static transport_peer make_data_peer(
        const transport_peer& control_peer, const ipv6_hpai& endpoint) noexcept
    {
        auto result = control_peer;
        if (route_back(endpoint))
            return result;
        if (result.address.ss_family == AF_INET6)
        {
            auto& address = reinterpret_cast<sockaddr_in6&>(result.address);
            std::memcpy(&address.sin6_addr, endpoint.endpoint.address.data(),
                        endpoint.endpoint.address.size());
            address.sin6_port = htons(endpoint.endpoint.port);
            result.length = sizeof(sockaddr_in6);
        }
        return result;
    }

    generic_server::generic_server(datagram_transport& transport, const server_config config,
                                   const server_clock_now_function clock_now) noexcept:
        transport_(transport), config_(config), clock_now_(clock_now)
    {
        if (config_.max_channels == 0u)
            config_.max_channels = 1u;
    }

    std::uint32_t generic_server::now_ms() const noexcept
    {
        if (clock_now_ != nullptr)
            return clock_now_();
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    void generic_server::observe_activity(channel& value) noexcept
    {
        value.last_activity_ms = now_ms();
    }

    bool generic_server::channel_active(const std::uint8_t channel_id) const noexcept
    {
        return (channel_id != 0u) && channels_[channel_id].active;
    }

    std::uint8_t generic_server::active_channels() const noexcept
    {
        std::uint8_t count {};
        for (std::uint16_t id = 1u; id < channels_.size(); ++id)
        {
            if (channels_[id].active)
                ++count;
        }
        return count;
    }

    std::expected<std::uint8_t, std::error_code> generic_server::allocate_channel() noexcept
    {
        const auto limit = std::min<std::uint16_t>(config_.max_channels, 255u);
        for (std::uint16_t id = 1u; id <= limit; ++id)
        {
            if (!channels_[id].active)
                return static_cast<std::uint8_t>(id);
        }
        return std::unexpected(make_error_code(error::send_queue_full));
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

    bool generic_server::peer_matches(const channel& value, const transport_peer& peer,
                                      const bool data_endpoint) const noexcept
    {
        const auto& expected_peer = data_endpoint ? value.data_peer : value.peer;
        if (!value.active || (expected_peer.length == 0u) || (expected_peer.length != peer.length) ||
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
            return (expected.sin6_port == actual.sin6_port) &&
                   (expected.sin6_scope_id == actual.sin6_scope_id) &&
                   (std::memcmp(&expected.sin6_addr, &actual.sin6_addr, sizeof(expected.sin6_addr)) == 0);
        }
        return std::memcmp(&expected_peer.address, &peer.address, peer.length) == 0;
    }

    task_returning_expected_void_t generic_server::send_datagram(
        const cspan_uint8_t packet, const transport_peer& peer) noexcept(false)
    {
        const auto* bytes = reinterpret_cast<const std::byte*>(packet.data());
        const auto sent = co_await transport_.send(
            cspan_byte_t {bytes, packet.size()}, reinterpret_cast<const sockaddr*>(&peer.address), peer.length);
        if (!sent)
            co_return std::unexpected(sent.error());
        if (*sent != packet.size())
            co_return std::unexpected(make_error_code(error::connection_failed));
        co_return expected_void_t {};
    }


    bool generic_server::answers_discovery() const noexcept
    {
        return config_.control_endpoint.protocol == 0x01u;
    }

    /// @brief Builds the peer a discovery answer is sent to from the HPAI the request carried.
    /// @param endpoint The endpoint the requester named.
    /// @param source The address the request arrived from.
    /// @return The endpoint to answer, or nothing when the HPAI is unusable.
    /// @details A zero address and port is the route-back HPAI: the requester is behind NAT and asks to be
    ///          answered wherever the datagram came from.
    [[nodiscard]] static std::optional<transport_peer> discovery_reply_peer(const hpai& endpoint,
                                                                            const transport_peer& source) noexcept
    {
        if (endpoint.protocol != 0x01u)
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
    server_event_task_t generic_server::answer_search(const discovery::search_request_frame& request,
                                                      const transport_peer& source) noexcept(false)
    {
        if (!answers_discovery())
            co_return std::unexpected(make_error_code(error::timeout));

        const auto destination = discovery_reply_peer(request.discovery_endpoint, source);
        if (!destination.has_value())
            co_return std::unexpected(make_error_code(error::unsupported_hpai));
        if (const auto built = build_discovery_packets(); !built.has_value())
            co_return std::unexpected(built.error());
        if (const auto sent = co_await send_datagram(search_response_packet_, *destination); !sent)
            co_return std::unexpected(sent.error());

        co_return std::unexpected(make_error_code(error::timeout));
    }

    server_event_task_t generic_server::answer_description(const discovery::description_request_frame& request,
                                                           const transport_peer& source) noexcept(false)
    {
        if (!answers_discovery())
            co_return std::unexpected(make_error_code(error::timeout));

        const auto destination = discovery_reply_peer(request.control_endpoint, source);
        if (!destination.has_value())
            co_return std::unexpected(make_error_code(error::unsupported_hpai));
        if (const auto built = build_discovery_packets(); !built.has_value())
            co_return std::unexpected(built.error());
        if (const auto sent = co_await send_datagram(description_response_packet_, *destination); !sent)
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

        description_blocks_.assign(dib::encoded_size(config_.description_blocks) + config_.device_info_blocks.size(), 0u);
        const auto written = dib::encode_all(description_blocks_, config_.description_blocks);
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
        if (const auto encoded = discovery::encode_description_response_packet(
            description_response_packet_, discovery::description_response_frame {description_blocks_});
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
                    matched = (parameter.data.size() == 2u) &&
                              dib::known_service_family(parameter.data[0u]) &&
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

    byte_buffer_t generic_server::requested_description_blocks(
        const discovery::extended_search_request_frame& request) const
    {
        byte_buffer_t requested_types {};
        for (const auto& parameter: request.parameters)
        {
            if (parameter.type == discovery::search_parameter_type::request_dibs)
                requested_types.insert(requested_types.end(), parameter.data.begin(), parameter.data.end());
        }
        if (requested_types.empty())
            return description_blocks_;

        byte_buffer_t result {};
        for (std::size_t offset {}; offset < description_blocks_.size();)
        {
            const auto block_size = description_blocks_[offset];
            if (std::ranges::find(requested_types, description_blocks_[offset + 1u]) != requested_types.end())
                result.insert(result.end(), description_blocks_.begin() + offset,
                              description_blocks_.begin() + offset + block_size);
            offset += block_size;
        }
        return result;
    }

    server_event_task_t generic_server::answer_extended_search(const discovery::extended_search_request_frame& request,
                                                               const transport_peer& source) noexcept(false)
    {
        if (!answers_discovery() || !matches_search_parameters(request))
            co_return std::unexpected(make_error_code(error::timeout));

        const auto destination = discovery_reply_peer(request.discovery_endpoint, source);
        if (!destination.has_value())
            co_return std::unexpected(make_error_code(error::unsupported_hpai));
        if (const auto built = build_discovery_packets(); !built.has_value())
            co_return std::unexpected(built.error());
        const auto description = requested_description_blocks(request);
        byte_buffer_t packet(frame::communication_header_size + connection::hpai_size + description.size(), 0u);
        if (const auto encoded = discovery::encode_extended_search_response_packet(
                packet, discovery::extended_search_response_frame {config_.control_endpoint, description});
            !encoded.has_value())
            co_return std::unexpected(encoded.error());
        if (const auto sent = co_await send_datagram(packet, *destination); !sent)
            co_return std::unexpected(sent.error());

        co_return std::unexpected(make_error_code(error::timeout));
    }

    generic_server::connection_offer generic_server::offer_channel(const transport_peer& peer) noexcept
    {
        const auto channel_id = allocate_channel();
        if (!channel_id.has_value())
            return {};

        connection_offer offer {*channel_id, connect_status::no_error, assigned_address(*channel_id), true};
        auto& value = channels_[offer.channel_id];
        value.active = true;
        value.peer = peer;
        value.assigned_address = offer.assigned;
        value.sequence = 0u;
        observe_activity(value);
        return offer;
    }

    server_event_task_t generic_server::open_ipv6_channel(const ipv6_connect_request_frame& request,
                                                          const std::uint16_t service_type,
                                                          const transport_peer& peer) noexcept(false)
    {
        if (service_type != connection::connect_request_service)
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        if (const auto valid = validate_connect_request(request.control_endpoint, request.data_endpoint); !valid.has_value())
        {
            auto response_endpoint = request.data_endpoint;
            response_endpoint.protocol = 0x01u;
            std::array<std::uint8_t, frame::communication_header_size + connection::ipv6_connect_response_body_size> response {};
            if (const auto encoded = connection::encode_ipv6_connect_response_packet(
                    response, ipv6_connect_response_frame {0u, connect_status::host_protocol_type, response_endpoint, {}});
                !encoded.has_value())
                co_return std::unexpected(encoded.error());
            if (const auto sent = co_await send_datagram(response, peer); !sent)
                co_return std::unexpected(sent.error());
            co_return std::unexpected(valid.error());
        }

        const auto offer = offer_channel(peer);
        if (offer.accepted)
            channels_[offer.channel_id].data_peer = make_data_peer(peer, request.data_endpoint);

        std::array<std::uint8_t, frame::communication_header_size + connection::ipv6_connect_response_body_size> response {};
        if (const auto encoded = connection::encode_ipv6_connect_response_packet(
                response, ipv6_connect_response_frame {offer.channel_id, offer.status, request.data_endpoint, offer.assigned});
            !encoded.has_value())
            co_return std::unexpected(encoded.error());
        if (const auto sent = co_await send_datagram(response, peer); !sent)
            co_return std::unexpected(sent.error());

        // The refusal is reported only after it has been sent: the client is owed the CONNECT_RESPONSE
        // that names the reason, and dropping out earlier would leave it waiting for one.
        if (!offer.accepted)
            co_return std::unexpected(make_error_code(error::send_queue_full));
        co_return server_event {offer.channel_id, {}};
    }

    server_event_task_t generic_server::open_channel(const connect_request_frame& request, const std::uint16_t service_type,
                                                     const transport_peer& peer) noexcept(false)
    {
        if (service_type != connection::connect_request_service)
            co_return std::unexpected(make_error_code(error::invalid_configuration));
        if (const auto valid = validate_connect_request(request.control_endpoint, request.data_endpoint); !valid.has_value())
        {
            auto response_endpoint = request.data_endpoint;
            response_endpoint.protocol = 0x01u;
            std::array<std::uint8_t, frame::communication_header_size + connection::connect_response_body_size> response {};
            if (const auto encoded = connection::encode_connect_response_packet(
                    response, connect_response_frame {0u, connect_status::host_protocol_type, response_endpoint, {}});
                !encoded.has_value())
                co_return std::unexpected(encoded.error());
            if (const auto sent = co_await send_datagram(response, peer); !sent)
                co_return std::unexpected(sent.error());
            co_return std::unexpected(valid.error());
        }
        // The codec can carry every KNX layer, but this server serves only the link layer: a bus monitor
        // or raw tunnel delivers L_Busmon and L_Raw frames, which the cEMI layer here does not decode.
        // Refusing the connection is honest; accepting it and then rejecting every frame is not.
        if (request.knx_layer != connection::tunnel_link_layer)
            co_return std::unexpected(make_error_code(error::unsupported_connection_type));

        const auto offer = offer_channel(peer);
        if (offer.accepted)
        {
            channels_[offer.channel_id].data_peer = make_data_peer(peer, request.data_endpoint);
            channels_[offer.channel_id].data_endpoint = request.data_endpoint;
        }

        std::array<std::uint8_t, frame::communication_header_size + connection::connect_response_body_size> response {};
        if (const auto encoded = connection::encode_connect_response_packet(
                response, connect_response_frame {offer.channel_id, offer.status, request.data_endpoint, offer.assigned});
            !encoded.has_value())
            co_return std::unexpected(encoded.error());
        if (const auto sent = co_await send_datagram(response, peer); !sent)
            co_return std::unexpected(sent.error());

        if (!offer.accepted)
            co_return std::unexpected(make_error_code(error::send_queue_full));
        co_return server_event {offer.channel_id, {}};
    }

    server_event_task_t generic_server::answer_connectionstate(const connectionstate_request_frame& request,
                                                               const transport_peer& peer) noexcept(false)
    {
        if (!channel_active(request.channel_id) || !peer_matches(channels_[request.channel_id], peer))
            co_return std::unexpected(make_error_code(error::sequence_error));

        std::array<std::uint8_t, frame::communication_header_size + 2u> response {};
        if (const auto encoded = connection::encode_connectionstate_response_packet(
                response, connectionstate_response_frame {request.channel_id, connect_status::no_error});
            !encoded.has_value())
            co_return std::unexpected(encoded.error());
        if (const auto sent = co_await send_datagram(response, peer); !sent)
            co_return std::unexpected(sent.error());

        observe_activity(channels_[request.channel_id]);
        co_return server_event {request.channel_id, {}};
    }

    server_event_task_t generic_server::answer_disconnect(const disconnect_request_frame& request,
                                                          const transport_peer& peer) noexcept(false)
    {
        if (!channel_active(request.channel_id) || !peer_matches(channels_[request.channel_id], peer))
            co_return std::unexpected(make_error_code(error::sequence_error));

        std::array<std::uint8_t, frame::communication_header_size + 2u> response {};
        if (const auto encoded = connection::encode_disconnect_response_packet(
                response, disconnect_response_frame {request.channel_id, connect_status::no_error});
            !encoded.has_value())
            co_return std::unexpected(encoded.error());
        if (const auto sent = co_await send_datagram(response, peer); !sent)
            co_return std::unexpected(sent.error());

        observe_activity(channels_[request.channel_id]);
        release_channel(request.channel_id);
        co_return server_event {request.channel_id, {}};
    }

    task_returning_expected_void_t generic_server::send_tunnelling_ack(const std::uint8_t channel_id,
                                                                       const std::uint8_t sequence_number,
                                                                       const transport_peer& peer) noexcept(false)
    {
        std::array<std::uint8_t, frame::communication_header_size + frame::tunnelling_ack_size> response {};
        if (const auto encoded = frame::encode_tunnelling_ack_packet(response, channel_id, sequence_number);
            !encoded.has_value())
            co_return std::unexpected(encoded.error());
        co_return co_await send_datagram(response, peer);
    }

    server_event_task_t generic_server::accept_tunnelled(const tunnelling_request_frame& request,
                                                          const transport_peer& peer) noexcept(false)
    {
        if (!channel_active(request.channel_id) || !peer_matches(channels_[request.channel_id], peer, true))
            co_return std::unexpected(make_error_code(error::sequence_error));

        auto& channel = channels_[request.channel_id];
        if (channel.incoming_sequence_valid)
        {
            // A repeat of the frame just seen is acknowledged again and dropped: the client is retrying
            // because it missed the acknowledgement, not because it has something new to say.
            if (request.sequence_number == channel.last_incoming_sequence)
            {
                if (const auto sent = co_await send_tunnelling_ack(request.channel_id, request.sequence_number, peer); !sent)
                    co_return std::unexpected(sent.error());
                observe_activity(channel);
                co_return server_event {request.channel_id, {}};
            }
            if (request.sequence_number != channel.next_incoming_sequence)
                co_return std::unexpected(make_error_code(error::sequence_error));
        }

        channel.incoming_sequence_valid = true;
        channel.last_incoming_sequence = request.sequence_number;
        channel.next_incoming_sequence = static_cast<std::uint8_t>(request.sequence_number + 1u);
        if (const auto sent = co_await send_tunnelling_ack(request.channel_id, request.sequence_number, peer); !sent)
            co_return std::unexpected(sent.error());

        observe_activity(channel);
        co_return server_event {request.channel_id, byte_buffer_t(request.cemi_bytes.begin(), request.cemi_bytes.end())};
    }

    server_event_task_t generic_server::dispatch(const datagram& value, const transport_peer& peer) noexcept(false)
    {
        // Discovery is connectionless and is answered before any channel bookkeeping: a server that never
        // replies to SEARCH cannot be found by ETS or by any other client, however well its tunnelling
        // works. Every answer goes to the endpoint the request names, not back to the multicast group.
        if (const auto* request = std::get_if<discovery::search_request_frame>(&value.payload))
            co_return co_await answer_search(*request, peer);
        if (const auto* request = std::get_if<discovery::extended_search_request_frame>(&value.payload))
            co_return co_await answer_extended_search(*request, peer);
        if (const auto* request = std::get_if<discovery::description_request_frame>(&value.payload))
            co_return co_await answer_description(*request, peer);

        if (const auto* request = std::get_if<ipv6_connect_request_frame>(&value.payload))
            co_return co_await open_ipv6_channel(*request, value.service_type, peer);
        if (const auto* request = std::get_if<connect_request_frame>(&value.payload))
            co_return co_await open_channel(*request, value.service_type, peer);
        if (const auto* request = std::get_if<connectionstate_request_frame>(&value.payload))
            co_return co_await answer_connectionstate(*request, peer);
        if (const auto* request = std::get_if<disconnect_request_frame>(&value.payload))
            co_return co_await answer_disconnect(*request, peer);
        if (const auto* request = std::get_if<tunnelling_request_frame>(&value.payload))
            co_return co_await accept_tunnelled(*request, peer);

        co_return std::unexpected(make_error_code(error::unsupported_service));
    }

    server_event_task_t generic_server::serve_once() noexcept(false)
    {
        if (shutdown_)
            co_return std::unexpected(make_error_code(error::shutdown));
        if (const auto active = poll(); !active.has_value())
            co_return std::unexpected(active.error());

        transport_peer peer {};
        auto* bytes = reinterpret_cast<std::byte*>(receive_buffer_.data());
        const auto received = co_await transport_.receive(span_byte_t {bytes, receive_buffer_.size()}, peer);
        if (!received)
            co_return std::unexpected(received.error());
        if ((received.value() == 0u) || (received.value() > receive_buffer_.size()))
            co_return std::unexpected(make_error_code(error::invalid_length));

        const auto decoded = decode_datagram({receive_buffer_.data(), received.value()});
        if (!decoded.has_value())
            co_return std::unexpected(decoded.error());

        co_return co_await dispatch(decoded.value(), peer);
    }


    task_returning_expected_void_t generic_server::serve() noexcept(false)
    {
        const auto stop_token = co_await get_stop_token;
        if (shutdown_)
            co_return std::unexpected(make_error_code(error::shutdown));
        while (!shutdown_)
        {
            if (stop_token.stop_requested())
            {
                shutdown_ = true;
                co_return std::unexpected(make_error_code(error::shutdown));
            }

            const auto event = co_await serve_once();
            if (!event.has_value())
            {
                const auto err = event.error();
                if ((err == make_error_code(error::timeout)) ||
                    (err == make_error_code(error::send_queue_full)) ||
                    (err == make_error_code(error::unsupported_connection_type)) ||
                    (err == make_error_code(error::unsupported_service)) ||
                    (err == make_error_code(error::sequence_error)) ||
                    (err == make_error_code(error::malformed_frame)) ||
                    (err == make_error_code(error::invalid_configuration)) ||
                    (err == make_error_code(error::unsupported_hpai)) ||
                    (err == make_error_code(error::invalid_length)))
                {
                    continue;
                }
                co_return std::unexpected(err);
            }
        }
        co_return expected_void_t {};
    }

    task_returning_expected_void_t generic_server::send(
        const std::uint8_t channel_id, const cspan_uint8_t cemi_bytes) noexcept(false)
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

        std::array<std::uint8_t, frame::max_datagram_size> buffer {};
        const auto encoded = frame::encode_tunnelling_request_packet(
            buffer, channel_id, channels_[channel_id].sequence++, cemi_bytes);
        if (!encoded.has_value())
            co_return std::unexpected(encoded.error());
        const auto size = frame::communication_header_size + frame::tunnelling_request_header_size + cemi_bytes.size();
        const auto sent = co_await send_datagram(cspan_uint8_t {buffer.data(), size}, channels_[channel_id].data_peer);
        if (!sent)
            co_return std::unexpected(sent.error());
        observe_activity(channels_[channel_id]);
        co_return expected_void_t {};
    }

    expected_void_t generic_server::disconnect(const std::uint8_t channel_id) noexcept
    {
        if (!channel_active(channel_id))
            return std::unexpected(make_error_code(error::invalid_configuration));
        release_channel(channel_id);
        return {};
    }

    expected_void_t generic_server::shutdown() noexcept
    {
        shutdown_ = true;
        for (std::uint16_t id = 1u; id < channels_.size(); ++id)
            release_channel(static_cast<std::uint8_t>(id));
        return {};
    }

    expected_void_t generic_server::reset() noexcept
    {
        for (std::uint16_t id = 1u; id < channels_.size(); ++id)
            release_channel(static_cast<std::uint8_t>(id));
        shutdown_ = false;
        return {};
    }

    expected_void_t generic_server::poll() noexcept
    {
        if (shutdown_)
            return std::unexpected(make_error_code(error::shutdown));
        if (config_.inactivity_timeout_ms == 0u)
            return {};

        const auto current = now_ms();
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
}
