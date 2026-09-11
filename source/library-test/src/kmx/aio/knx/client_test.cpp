/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/timer.hpp>
#include <kmx/aio/knx/client.hpp>
#include <kmx/aio/test/knx/telegram.hpp>
#include <kmx/aio/test/knx/transport.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <deque>
#include <netinet/in.h>
#include <system_error>
#include <stop_token>
#include <type_traits>
#include <vector>

namespace kmx::aio::test::knx::client_test
{
    using namespace kmx::aio::knx;

    std::uint32_t test_now_ms {};

    [[nodiscard]] std::uint32_t test_clock_now() noexcept
    {
        return test_now_ms;
    }

    class loopback_transport final: public test::knx::recording_transport
    {
    public:
        bool timeout_next_receive {};
        bool always_timeout {};
        bool ack_received {};
        bool heartbeat_failure {};
        bool connect_failure {};
        bool short_send {};
        bool wrong_peer {};
        bool invalid_peer_length {};
        bool short_peer_length {};
        bool empty_receive {};
        bool oversized_receive {};
        bool ipv6_peer {};
        bool ipv6_connect {};
        bool data_peer_as_control {};
        bool hold_receive {};
        completion::executor* wait_executor {};
        std::uint16_t advertised_data_port = 3672u;
        std::uint32_t last_receive_deadline {};
        std::error_code send_error {};
        std::error_code receive_error {};

        void enqueue(std::vector<std::uint8_t> packet)
        {
            responses_.push_back(std::move(packet));
        }

        /// @brief A synthesised answer, or the reason none could be built.
        /// @details An empty vector means the request needed no answer.
        using response_result = std::expected<std::vector<std::uint8_t>, std::error_code>;

        [[nodiscard]] response_result make_connect_response() const
        {
            const auto status = connect_failure ? connect_status::no_more_connections : connect_status::no_error;
            std::vector<std::uint8_t> response(ipv6_connect ? 32u : 20u, 0u);
            if (ipv6_connect)
            {
                const ipv6_connect_response_frame value {
                    .channel_id = 3u,
                    .status = status,
                    .data_endpoint = ipv6_hpai {ipv6_endpoint {{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u},
                                                               advertised_data_port},
                                                0x01u},
                    .assigned_address = individual_address {1u, 1u, 10u},
                };
                if (const auto result = connection::encode_ipv6_connect_response_packet(response, value); !result.has_value())
                    return std::unexpected(result.error());
                return response;
            }

            const connect_response_frame value {
                .channel_id = 3u,
                .status = status,
                .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, advertised_data_port}, 0x01u},
                .assigned_address = individual_address {1u, 1u, 10u},
            };
            if (const auto result = connection::encode_connect_response_packet(response, value); !result.has_value())
                return std::unexpected(result.error());
            return response;
        }

        [[nodiscard]] static response_result make_tunnelling_ack(const cspan_uint8_t packet)
        {
            const auto request = frame::decode_tunnelling_request_packet(packet);
            if (!request.has_value())
                return std::unexpected(request.error());

            std::vector<std::uint8_t> response(10u, 0u);
            if (const auto result = frame::encode_tunnelling_ack_packet(response, request->channel_id, request->sequence_number);
                !result.has_value())
                return std::unexpected(result.error());
            return response;
        }

        [[nodiscard]] static response_result make_disconnect_response(const cspan_uint8_t packet)
        {
            const auto request = connection::decode_disconnect_request_packet(packet);
            if (!request.has_value())
                return std::unexpected(request.error());

            std::vector<std::uint8_t> response(8u, 0u);
            const disconnect_response_frame value {request->channel_id, connect_status::no_error};
            if (const auto result = connection::encode_disconnect_response_packet(response, value); !result.has_value())
                return std::unexpected(result.error());
            return response;
        }

        [[nodiscard]] response_result make_connectionstate_response(const cspan_uint8_t packet) const
        {
            const auto request = connection::decode_connectionstate_request_packet(packet);
            if (!request.has_value())
                return std::unexpected(request.error());

            std::vector<std::uint8_t> response(8u, 0u);
            const connectionstate_response_frame value {
                request->channel_id,
                heartbeat_failure ? connect_status::knx_connection : connect_status::no_error,
            };
            if (const auto result = connection::encode_connectionstate_response_packet(response, value); !result.has_value())
                return std::unexpected(result.error());
            return response;
        }

        /// @brief Builds the answer this stand-in server gives to one request.
        [[nodiscard]] response_result make_response(const std::uint16_t service, const cspan_uint8_t packet)
        {
            switch (service)
            {
                case connection::connect_request_service:
                    return make_connect_response();
                case frame::tunnelling_request_service:
                    return make_tunnelling_ack(packet);
                case connection::disconnect_request_service:
                    return make_disconnect_response(packet);
                case connection::connectionstate_request_service:
                    return make_connectionstate_response(packet);
                default:
                    return std::unexpected(make_error_code(error::unsupported_service));
            }
        }

        [[nodiscard]] task_returning_expected_size_t send(
            const cspan_byte_t payload, const sockaddr* peer, const ::socklen_t peer_length) noexcept(false) override
        {
            if (send_error)
                co_return std::unexpected(send_error);

            record_send(payload, peer, peer_length);
            const cspan_uint8_t packet {reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()};
            const auto header = frame::decode_communication_header(packet);
            if (!header.has_value())
                co_return std::unexpected(header.error());

            // An acknowledgement ends an exchange rather than starting one, so there is nothing to answer.
            if (header->service_type == frame::tunnelling_ack_service)
            {
                ack_received = true;
                co_return expected_size_t {payload.size()};
            }

            auto response = make_response(header->service_type, packet);
            if (!response.has_value())
                co_return std::unexpected(response.error());
            if (!response->empty())
                responses_.push_back(std::move(*response));

            co_return expected_size_t {short_send ? payload.size() - 1u : payload.size()};
        }

        /// @brief An outcome a configured fault dictates, or nothing when the queue decides.
        using optional_size_result = std::optional<expected_size_t>;

        /// @brief Writes the peer every receive starts from, before any response-specific adjustment.
        void set_initial_peer(transport_peer& peer) const noexcept
        {
            peer.length = sizeof(sockaddr_storage);
            if (invalid_peer_length)
                peer.length = sizeof(sockaddr_storage) + 1u;
            if (short_peer_length)
                peer.length = sizeof(sockaddr_in) - 1u;

            peer.address = {};
            if (wrong_peer)
                peer.address.ss_family = AF_UNIX;
        }

        /// @brief Returns the outcome a configured fault dictates, if one does.
        /// @param buffer The caller's buffer, for the oversized-receive fault to overstate.
        /// @return The outcome, or nothing when the response queue should decide instead.
        [[nodiscard]] optional_size_result configured_outcome(const span_byte_t buffer) noexcept
        {
            const auto timed_out = expected_size_t {std::unexpected(make_error_code(error::timeout))};
            if (timeout_next_receive)
            {
                timeout_next_receive = false;
                return timed_out;
            }
            if (always_timeout)
                return timed_out;
            if (empty_receive)
                return expected_size_t {0u};
            if (oversized_receive)
                return expected_size_t {buffer.size() + 1u};
            if (responses_.empty())
                return timed_out;
            return {};
        }

        /// @brief Reports whether a response is one the client should see arriving on its data endpoint.
        /// @param response The response about to be delivered.
        /// @return `true` when it belongs to the data channel rather than the control channel.
        [[nodiscard]] static bool from_data_peer(const std::vector<std::uint8_t>& response) noexcept
        {
            const auto header = frame::decode_communication_header(response);
            return header.has_value() && ((header->service_type == frame::tunnelling_ack_service) ||
                                          (header->service_type == frame::tunnelling_request_service));
        }

        /// @brief Adjusts the peer to the endpoint a particular response would really have come from.
        /// @param peer The peer to adjust; its length is left as @ref set_initial_peer wrote it.
        /// @param response The response about to be delivered.
        void apply_response_peer(transport_peer& peer, const std::vector<std::uint8_t>& response) const noexcept
        {
            const auto header = frame::decode_communication_header(response);
            if (ipv6_connect && header.has_value() &&
                ((header->service_type == connection::connect_response_service) ||
                 (header->service_type == connection::disconnect_response_service)))
            {
                auto& control_peer = reinterpret_cast<sockaddr_in6&>(peer.address);
                control_peer.sin6_family = AF_INET6;
                control_peer.sin6_port = htons(3671u);
                control_peer.sin6_addr = in6addr_loopback;
            }

            if (wrong_peer || !from_data_peer(response))
                return;
            if (data_peer_as_control)
            {
                peer.address = {};
                return;
            }

            if (ipv6_peer)
            {
                auto& data_peer = reinterpret_cast<sockaddr_in6&>(peer.address);
                data_peer.sin6_family = AF_INET6;
                data_peer.sin6_port = htons(advertised_data_port);
                data_peer.sin6_addr = in6addr_loopback;
                return;
            }

            auto& data_peer = reinterpret_cast<sockaddr_in&>(peer.address);
            data_peer.sin_family = AF_INET;
            data_peer.sin_port = htons(advertised_data_port);
            data_peer.sin_addr.s_addr = htonl(0x7F000001u);
        }

        [[nodiscard]] task_returning_expected_size_t receive(
            const span_byte_t buffer, transport_peer& peer) noexcept(false) override
        {
            if (hold_receive && (wait_executor != nullptr))
            {
                completion::timer timer {*wait_executor};
                const auto waited = co_await timer.wait(std::chrono::milliseconds {25});
                if (!waited.has_value())
                    co_return std::unexpected(waited.error());
                co_return std::unexpected(make_error_code(error::timeout));
            }
            if (receive_error)
                co_return std::unexpected(receive_error);

            set_initial_peer(peer);
            if (const auto configured = configured_outcome(buffer); configured.has_value())
                co_return *configured;

            const auto response = std::move(responses_.front());
            responses_.pop_front();
            apply_response_peer(peer, response);
            co_return deliver(response, buffer);
        }

        [[nodiscard]] task_returning_expected_size_t receive_until(
            const span_byte_t buffer, transport_peer& peer, const std::uint32_t deadline_ms) noexcept(false) override
        {
            last_receive_deadline = deadline_ms;
            co_return co_await receive(buffer, peer);
        }

    private:
        std::deque<std::vector<std::uint8_t>> responses_ {};
    };

    namespace detail
    {
        /// @brief The connect request every test in this file sends.
        inline const connect_request_frame loopback_connect_request {
            .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3671u}, 0x01u},
            .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3672u}, 0x01u},
        };

        /// @brief The transport, peer, client and executor a loopback client test starts from.
        /// @details Catch2 builds one of these per test case, so each case still gets the fresh transport
        ///          and unconnected client that the hand-written prologue gave it.
        /// @note Only for tests that leave the peer as it is. The client copies the peer at construction,
        ///       so a test that configures one - a family, a port, a truncated length - has to build its
        ///       own client after writing it, and those cases keep the prologue on purpose.
        struct loopback_client_fixture
        {
            loopback_transport transport;
            sockaddr_storage peer {};
            tunnelling_client client {transport, peer, sizeof(peer)};
            completion::executor executor;

            /// @brief Spawns @p work and runs the loop until the work stops it.
            /// @param work The task to drive; it is responsible for calling executor.stop().
            void spawn_and_run(task<void> work) noexcept(false)
            {
                executor.spawn(std::move(work));
                executor.run();
            }
        };

        /// @brief Returns the cEMI octets of the last TUNNELLING_REQUEST a transport was handed.
        /// @param transport The transport to inspect.
        /// @return The cEMI octets, empty when no tunnelling request was sent.
        [[nodiscard]] inline std::vector<std::uint8_t> last_sent_cemi(const loopback_transport& transport) noexcept(false)
        {
            for (auto packet = transport.sent_packets().rbegin(); packet != transport.sent_packets().rend(); ++packet)
            {
                const auto request = frame::decode_tunnelling_request_packet(*packet);
                if (request.has_value())
                    return {request->cemi_bytes.begin(), request->cemi_bytes.end()};
            }

            return {};
        }

        /// @brief Connects, sends one cEMI and disconnects, recording whether the whole lifecycle succeeded.
        task<void> run_loopback_lifecycle(tunnelling_client& client, const connect_request_frame& request, const cspan_uint8_t cemi,
                                          bool& succeeded, completion::executor& executor) noexcept(false)
        {
            const auto connected = co_await client.connect(request);
            if (!connected)
            {
                executor.stop();
                co_return;
            }
            const auto sent = co_await client.send(cemi);
            if (!sent)
            {
                executor.stop();
                co_return;
            }
            const auto disconnected = co_await client.disconnect();
            succeeded = disconnected.has_value();
            executor.stop();
        }

        /// @brief Connects, then sends with the first receive timed out, so the request has to be retried.
        task<void> retry_timed_out_request(loopback_transport& transport, tunnelling_client& client, const connect_request_frame& request,
                                           const cspan_uint8_t cemi, bool& succeeded, completion::executor& executor) noexcept(false)
        {
            const auto connected = co_await client.connect(request);
            if (!connected)
            {
                executor.stop();
                co_return;
            }
            transport.timeout_next_receive = true;
            const auto sent = co_await client.send(cemi);
            succeeded = sent.has_value();
            executor.stop();
        }

        /// @brief Connects, feeds the transport an acknowledgement for another channel, then sends.
        task<void> ignore_stale_ack(loopback_transport& transport, tunnelling_client& client, const connect_request_frame& request,
                                    const cspan_uint8_t stale_ack, bool& succeeded, completion::executor& executor) noexcept(false)
        {
            if (!(co_await client.connect(request)).has_value())
            {
                executor.stop();
                co_return;
            }
            transport.enqueue(std::vector<std::uint8_t>(stale_ack.begin(), stale_ack.end()));
            succeeded = (co_await client.send(sample_cemi)).has_value();
            executor.stop();
        }

        /// @brief Connects, then disconnects with the first receive timed out, so the request has to be retried.
        task<void> retry_timed_out_disconnect(loopback_transport& transport, tunnelling_client& client,
                                              const connect_request_frame& request, bool& succeeded,
                                              completion::executor& executor) noexcept(false)
        {
            if (!(co_await client.connect(request)).has_value())
            {
                executor.stop();
                co_return;
            }
            transport.timeout_next_receive = true;
            succeeded = (co_await client.disconnect()).has_value();
            executor.stop();
        }

        /// @brief Connects and completes one heartbeat exchange.
        task<void> complete_heartbeat(tunnelling_client& client, const connect_request_frame& request, bool& succeeded,
                                      completion::executor& executor) noexcept(false)
        {
            if (!(co_await client.connect(request)).has_value())
            {
                executor.stop();
                co_return;
            }
            succeeded = (co_await client.heartbeat()).has_value();
            executor.stop();
        }

        /// @brief Connects, then heartbeats against a transport that refuses, and records what came back.
        task<void> propagate_heartbeat_failure(loopback_transport& transport, tunnelling_client& client,
                                               const connect_request_frame& request, bool& failed, bool& stayed_connected,
                                               completion::executor& executor) noexcept(false)
        {
            if (!(co_await client.connect(request)).has_value())
            {
                executor.stop();
                co_return;
            }
            transport.heartbeat_failure = true;
            const auto result = co_await client.heartbeat();
            failed = !result.has_value() && result.error() == make_error_code(error::connection_failed);
            stayed_connected = client.state() == session_state::connected;
            executor.stop();
        }

        /// @brief Connects against a transport that refuses, and records the error and the resulting state.
        task<void> propagate_connect_failure(tunnelling_client& client, const connect_request_frame& request, bool& failed,
                                             completion::executor& executor) noexcept(false)
        {
            const auto result = co_await client.connect(request);
            failed = !result.has_value();
            CHECK(result.error() == make_error_code(error::connection_failed));
            CHECK(client.state() == session_state::idle);
            executor.stop();
        }

        /// @brief Connects, then heartbeats with the first receive timed out, so the request has to be retried.
        task<void> retry_timed_out_heartbeat(loopback_transport& transport, tunnelling_client& client, const connect_request_frame& request,
                                             bool& succeeded, completion::executor& executor) noexcept(false)
        {
            if (!(co_await client.connect(request)).has_value())
            {
                executor.stop();
                co_return;
            }
            transport.timeout_next_receive = true;
            succeeded = (co_await client.heartbeat()).has_value();
            executor.stop();
        }

        /// @brief Connects, then heartbeats three times against a refusing transport to reach the terminal state.
        task<void> escalate_heartbeat_failures(loopback_transport& transport, tunnelling_client& client,
                                               const connect_request_frame& request, bool& terminal,
                                               completion::executor& executor) noexcept(false)
        {
            if (!(co_await client.connect(request)).has_value())
            {
                executor.stop();
                co_return;
            }
            transport.heartbeat_failure = true;
            (void) co_await client.heartbeat();
            (void) co_await client.heartbeat();
            const auto result = co_await client.heartbeat();
            terminal = !result.has_value() && result.error() == make_error_code(error::heartbeat_failed) &&
                       client.state() == session_state::closed;
            executor.stop();
        }

        /// @brief Calls every public operation on a shut-down client and records that each one was refused.
        task<void> reject_after_shutdown(tunnelling_client& client, const connect_request_frame& request, const cspan_uint8_t cemi,
                                         bool& rejected, completion::executor& executor) noexcept(false)
        {
            const auto send_result = co_await client.send(cemi);
            const auto heartbeat_result = co_await client.heartbeat();
            const auto disconnect_result = co_await client.disconnect();
            const auto connect_result = co_await client.connect(request);
            rejected =
                !send_result.has_value() && !heartbeat_result.has_value() && !disconnect_result.has_value() && !connect_result.has_value();
            executor.stop();
        }

        /// @brief Calls both receive APIs on a shut-down client and records that each one was refused.
        task<void> reject_receive_after_shutdown(tunnelling_client& client, bool& rejected, completion::executor& executor) noexcept(false)
        {
            const auto datagram = co_await client.receive_datagram();
            const auto cemi = co_await client.receive_cemi();
            rejected = !datagram.has_value() && !cemi.has_value() && datagram.error() == make_error_code(error::shutdown) &&
                       cemi.error() == make_error_code(error::shutdown);
            executor.stop();
        }

        /// @brief Connects, shuts down, resets, and connects again to a transport advertising a new data port.
        task<void> reset_and_reconnect(loopback_transport& transport, tunnelling_client& client, const connect_request_frame& request,
                                       bool& reconnected, completion::executor& executor) noexcept(false)
        {
            if (!(co_await client.connect(request)).has_value())
            {
                executor.stop();
                co_return;
            }
            client.shutdown();
            client.reset();
            transport.advertised_data_port = 3673u;
            reconnected = (co_await client.connect(request)).has_value() && client.state() == session_state::connected &&
                          (co_await client.send(sample_cemi)).has_value();
            executor.stop();
        }

        /// @brief Connects, then sends against a transport whose write fails, and records the error that came back.
        task<void> preserve_send_error(loopback_transport& send_transport, tunnelling_client& send_client,
                                       const connect_request_frame& request, const cspan_uint8_t cemi,
                                       const std::error_code& transport_error, bool& send_preserved,
                                       completion::executor& send_executor) noexcept(false)
        {
            if (!(co_await send_client.connect(request)).has_value())
            {
                send_executor.stop();
                co_return;
            }
            send_transport.send_error = transport_error;
            const auto result = co_await send_client.send(cemi);
            send_preserved = !result.has_value() && result.error() == transport_error;
            send_executor.stop();
        }

        /// @brief Connects, then heartbeats against a transport whose read fails, and records the error that came back.
        task<void> preserve_heartbeat_error(loopback_transport& heartbeat_transport, tunnelling_client& heartbeat_client,
                                            const connect_request_frame& request, const std::error_code& transport_error,
                                            bool& heartbeat_preserved, completion::executor& heartbeat_executor) noexcept(false)
        {
            if (!(co_await heartbeat_client.connect(request)).has_value())
            {
                heartbeat_executor.stop();
                co_return;
            }
            heartbeat_transport.receive_error = transport_error;
            const auto result = co_await heartbeat_client.heartbeat();
            heartbeat_preserved = !result.has_value() && result.error() == transport_error;
            heartbeat_executor.stop();
        }

        /// @brief Connects, then disconnects against a transport whose write fails, and records the error that came back.
        task<void> preserve_disconnect_send_error(loopback_transport& send_transport, tunnelling_client& send_client,
                                                  const connect_request_frame& request, const std::error_code& transport_error,
                                                  bool& send_preserved, completion::executor& send_executor) noexcept(false)
        {
            if (!(co_await send_client.connect(request)).has_value())
            {
                send_executor.stop();
                co_return;
            }
            send_transport.send_error = transport_error;
            const auto result = co_await send_client.disconnect();
            send_preserved = !result.has_value() && result.error() == transport_error;
            send_executor.stop();
        }

        /// @brief Connects, then disconnects against a transport whose read fails, and records the error that came back.
        task<void> preserve_disconnect_receive_error(loopback_transport& receive_transport, tunnelling_client& receive_client,
                                                     const connect_request_frame& request, const std::error_code& transport_error,
                                                     bool& receive_preserved, completion::executor& receive_executor) noexcept(false)
        {
            if (!(co_await receive_client.connect(request)).has_value())
            {
                receive_executor.stop();
                co_return;
            }
            receive_transport.receive_error = transport_error;
            const auto result = co_await receive_client.disconnect();
            receive_preserved = !result.has_value() && result.error() == transport_error;
            receive_executor.stop();
        }

        /// @brief Connects, then disconnects against a transport that reports more bytes than it was given.
        task<void> reject_oversized_disconnect_receive(loopback_transport& transport, tunnelling_client& client,
                                                       const connect_request_frame& request, bool& rejected,
                                                       completion::executor& executor) noexcept(false)
        {
            if (!(co_await client.connect(request)).has_value())
            {
                executor.stop();
                co_return;
            }
            transport.oversized_receive = true;
            const auto result = co_await client.disconnect();
            rejected = !result.has_value() && result.error() == make_error_code(error::invalid_length);
            executor.stop();
        }

        /// @brief Connects, asks the stop source to stop, then disconnects and records the cancellation.
        task<void> observe_disconnect_cancellation(tunnelling_client& client, const connect_request_frame& request,
                                                   std::stop_source& source, bool& cancelled,
                                                   completion::executor& executor) noexcept(false)
        {
            if (!(co_await client.connect(request)).has_value())
            {
                executor.stop();
                co_return;
            }
            source.request_stop();
            const auto result = co_await client.disconnect();
            cancelled = !result.has_value() && result.error() == make_error_code(error::shutdown);
            executor.stop();
        }

        /// @brief Connects, then sends an empty payload and records that it was refused as malformed.
        task<void> reject_empty_cemi(tunnelling_client& client, const cspan_uint8_t cemi, bool& rejected,
                                     completion::executor& executor) noexcept(false)
        {
            const connect_request_frame request {
                .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3671u}, 0x01u},
                .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3672u}, 0x01u},
            };
            if (!(co_await client.connect(request)).has_value())
            {
                executor.stop();
                co_return;
            }
            const auto result = co_await client.send(cemi);
            rejected = !result.has_value() && result.error() == make_error_code(error::malformed_frame);
            executor.stop();
        }

        /// @brief Connects over IPv6 and sends one cEMI through the resulting session.
        task<void> connect_and_send_over_ipv6(tunnelling_client& client, const ipv6_connect_request_frame& request, bool& succeeded,
                                              completion::executor& executor) noexcept(false)
        {
            const auto connected = co_await client.connect(request);
            const auto sent = connected ? co_await client.send(sample_cemi) : expected_void_t {std::unexpected(connected.error())};
            succeeded = connected.has_value() && sent.has_value();
            executor.stop();
        }

        /// @brief Connects, then feeds the transport a tunnelling datagram from the control peer.
        task<void> reject_control_peer_tunnelling(loopback_transport& transport, tunnelling_client& client,
                                                  const connect_request_frame& request, const cspan_uint8_t packet, bool& rejected,
                                                  completion::executor& executor) noexcept(false)
        {
            if (!(co_await client.connect(request)).has_value())
            {
                executor.stop();
                co_return;
            }
            transport.enqueue(std::vector<std::uint8_t>(packet.begin(), packet.end()));
            const auto result = co_await client.receive_datagram();
            rejected = !result.has_value() && result.error() == make_error_code(error::connection_failed);
            executor.stop();
        }

        /// @brief Reads one owning datagram and records whether it decoded as the expected acknowledgement.
        task<void> receive_owning_datagram(tunnelling_client& client, bool& received_ack, completion::executor& executor) noexcept(false)
        {
            const auto result = co_await client.receive_datagram();
            if (result.has_value())
            {
                const auto* ack = std::get_if<tunnelling_ack_frame>(&result->payload);
                received_ack = (ack != nullptr) && (ack->channel_id == 3u) && (ack->sequence_number == 7u);
            }
            executor.stop();
        }

        /// @brief Connects, feeds the transport an indication, and compares the owning cEMI read back.
        task<void> receive_owning_cemi(loopback_transport& transport, tunnelling_client& client, const cspan_uint8_t cemi,
                                       const cspan_uint8_t packet, bool& matched, completion::executor& executor) noexcept(false)
        {
            const connect_request_frame connect_request {
                .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3671u}, 0x01u},
                .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3672u}, 0x01u},
            };
            if (!(co_await client.connect(connect_request)).has_value())
            {
                executor.stop();
                co_return;
            }
            transport.enqueue(std::vector<std::uint8_t>(packet.begin(), packet.end()));
            const auto result = co_await client.receive_cemi();
            matched = result.has_value() && (result->size() == cemi.size()) && std::equal(result->begin(), result->end(), cemi.begin());
            executor.stop();
        }

        /// @brief Connects, feeds the transport an indication, and keeps the owning cEMI copy it returns.
        task<void> receive_owning_cemi_copy(loopback_transport& transport, tunnelling_client& client, const cspan_uint8_t request,
                                            std::vector<std::uint8_t>& received_payload, completion::executor& executor) noexcept(false)
        {
            const connect_request_frame connect_request {
                .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3671u}, 0x01u},
                .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3672u}, 0x01u},
            };
            if (!(co_await client.connect(connect_request)).has_value())
            {
                executor.stop();
                co_return;
            }
            transport.enqueue(std::vector<std::uint8_t>(request.begin(), request.end()));
            received_payload = *(co_await client.receive_cemi());
            executor.stop();
        }

        /// @brief Connects and feeds the transport the same indication twice, so the second is suppressed.
        task<void> suppress_duplicate_indication(loopback_transport& transport, tunnelling_client& client, const cspan_uint8_t indication,
                                                 bool& first_received, bool& duplicate_suppressed,
                                                 completion::executor& executor) noexcept(false)
        {
            if (!(co_await client.connect(connect_request_frame {
                      .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3671u}, 0x01u},
                      .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3672u}, 0x01u},
                  }))
                     .has_value())
            {
                executor.stop();
                co_return;
            }
            transport.enqueue(std::vector<std::uint8_t>(indication.begin(), indication.end()));
            transport.enqueue(std::vector<std::uint8_t>(indication.begin(), indication.end()));
            first_received = (co_await client.receive_cemi()).has_value();
            const auto duplicate = co_await client.receive_cemi();
            duplicate_suppressed = !duplicate.has_value() && duplicate.error() == make_error_code(error::timeout);
            executor.stop();
        }

        /// @brief Connects and feeds the transport two indications whose sequence numbers go backwards.
        task<void> reject_out_of_order_indication(loopback_transport& transport, tunnelling_client& client, const cspan_uint8_t first,
                                                  const cspan_uint8_t out_of_order, bool& rejected,
                                                  completion::executor& executor) noexcept(false)
        {
            if (!(co_await client.connect(connect_request_frame {
                      .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3671u}, 0x01u},
                      .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3672u}, 0x01u},
                  }))
                     .has_value())
            {
                executor.stop();
                co_return;
            }
            transport.enqueue(std::vector<std::uint8_t>(first.begin(), first.end()));
            transport.enqueue(std::vector<std::uint8_t>(out_of_order.begin(), out_of_order.end()));
            if (!(co_await client.receive_cemi()).has_value())
            {
                executor.stop();
                co_return;
            }
            const auto result = co_await client.receive_cemi();
            rejected = !result.has_value() && result.error() == make_error_code(error::sequence_error);
            executor.stop();
        }

        /// @brief Connects and queues a control datagram ahead of the tunnelling request, which must be skipped.
        task<void> skip_control_before_cemi(loopback_transport& transport, tunnelling_client& client, const cspan_uint8_t request,
                                            const cspan_uint8_t ack, bool& received, completion::executor& executor) noexcept(false)
        {
            if (!(co_await client.connect(connect_request_frame {
                      .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3671u}, 0x01u},
                      .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3672u}, 0x01u},
                  }))
                     .has_value())
            {
                executor.stop();
                co_return;
            }
            transport.enqueue(std::vector<std::uint8_t>(ack.begin(), ack.end()));
            transport.enqueue(std::vector<std::uint8_t>(request.begin(), request.end()));
            const auto result = co_await client.receive_cemi();
            received = result.has_value();
            executor.stop();
        }

        /// @brief Connects and queues a heartbeat response ahead of the tunnelling request.
        task<void> process_heartbeat_before_cemi(loopback_transport& transport, tunnelling_client& client, const cspan_uint8_t request,
                                                 bool& received, completion::executor& executor) noexcept(false)
        {
            const connect_request_frame connect_request {
                .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3671u}, 0x01u},
                .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3672u}, 0x01u},
            };
            if (!(co_await client.connect(connect_request)).has_value())
            {
                executor.stop();
                co_return;
            }
            std::array<std::uint8_t, 8u> heartbeat {};
            REQUIRE(
                connection::encode_connectionstate_response_packet(heartbeat, connectionstate_response_frame {3u, connect_status::no_error})
                    .has_value());
            transport.enqueue(std::vector<std::uint8_t>(heartbeat.begin(), heartbeat.end()));
            transport.enqueue(std::vector<std::uint8_t>(request.begin(), request.end()));
            received = (co_await client.receive_cemi()).has_value();
            executor.stop();
        }

        /// @brief Connects and feeds the transport a tunnelling request carrying another channel id.
        task<void> reject_cross_channel_cemi(loopback_transport& transport, tunnelling_client& client, const cspan_uint8_t request,
                                             bool& rejected, completion::executor& executor) noexcept(false)
        {
            const connect_request_frame connect_request {
                .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3671u}, 0x01u},
                .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3672u}, 0x01u},
            };
            if (!(co_await client.connect(connect_request)).has_value())
            {
                executor.stop();
                co_return;
            }
            transport.short_send = true;
            transport.enqueue(std::vector<std::uint8_t>(request.begin(), request.end()));
            const auto result = co_await client.receive_cemi();
            rejected = !result.has_value() && result.error() == make_error_code(error::sequence_error);
            executor.stop();
        }

        /// @brief Connects and feeds the transport a DISCONNECT_REQUEST, which must not surface as cEMI.
        task<void> reject_disconnect_control_as_cemi(loopback_transport& transport, tunnelling_client& client,
                                                     const cspan_uint8_t disconnect, bool& rejected,
                                                     completion::executor& executor) noexcept(false)
        {
            if (!(co_await client.connect(connect_request_frame {
                      .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3671u}, 0x01u},
                      .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3672u}, 0x01u},
                  }))
                     .has_value())
            {
                executor.stop();
                co_return;
            }
            transport.enqueue(std::vector<std::uint8_t>(disconnect.begin(), disconnect.end()));
            const auto result = co_await client.receive_cemi();
            rejected = !result.has_value() && result.error() == make_error_code(error::unsupported_service);
            executor.stop();
        }

        /// @brief Connects and writes one DPT 1.001 group value.
        task<void> write_typed_group_value(tunnelling_client& client, bool& succeeded, completion::executor& executor) noexcept(false)
        {
            if (!(co_await client.connect(detail::loopback_connect_request)))
            {
                executor.stop();
                co_return;
            }

            const auto value = dpt::encode<1u>(true);
            const auto written = co_await client.write_group_value(group_address {0x0A03u}, value.value());
            succeeded = written.has_value();
            executor.stop();
        }

        /// @brief Connects and issues one group-value read.
        task<void> read_group_value(tunnelling_client& client, bool& succeeded, completion::executor& executor) noexcept(false)
        {
            if (!(co_await client.connect(detail::loopback_connect_request)))
            {
                executor.stop();
                co_return;
            }

            succeeded = (co_await client.read_group_value(group_address {0x0A03u})).has_value();
            executor.stop();
        }

        /// @brief Connects, feeds the transport an indication, and keeps the decoded telegram.
        task<void> decode_received_telegram(loopback_transport& transport, tunnelling_client& client,
                                            const std::vector<std::uint8_t>& indication, std::expected<telegram, std::error_code>& received,
                                            completion::executor& executor) noexcept(false)
        {
            if (!(co_await client.connect(detail::loopback_connect_request)))
            {
                executor.stop();
                co_return;
            }

            transport.enqueue(indication);
            received = co_await client.receive_telegram();
            executor.stop();
        }
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client completes a loopback lifecycle", "[knx][client][integration]")
    {
        const auto& cemi = sample_cemi;
        bool succeeded {};

        spawn_and_run(detail::run_loopback_lifecycle(client, detail::loopback_connect_request, cemi, succeeded, executor));
        CHECK(succeeded);
        CHECK(client.state() == session_state::closed);
        REQUIRE(transport.sent_peers().size() >= 3u);
        const auto& tunnelling_destination =
            reinterpret_cast<const sockaddr_in&>(transport.sent_peers()[1u]);
        CHECK(tunnelling_destination.sin_family == AF_INET);
        CHECK(ntohs(tunnelling_destination.sin_port) == 3672u);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client retries a timed-out request", "[knx][client][integration]")
    {
        const auto& cemi = sample_cemi;
        bool succeeded {};

        spawn_and_run(detail::retry_timed_out_request(transport, client, detail::loopback_connect_request, cemi, succeeded, executor));
        CHECK(succeeded);
        CHECK(client.state() == session_state::connected);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client ignores a stale tunnelling acknowledgement",
                     "[knx][client][integration]")
    {
        std::array<std::uint8_t, 10u> stale_ack {};
        REQUIRE(frame::encode_tunnelling_ack_packet(stale_ack, 3u, 7u).has_value());

        bool succeeded {};
        spawn_and_run(detail::ignore_stale_ack(transport, client, detail::loopback_connect_request, stale_ack, succeeded, executor));
        CHECK(succeeded);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client retries a timed-out disconnect", "[knx][client][integration]")
    {
        bool succeeded {};

        spawn_and_run(detail::retry_timed_out_disconnect(transport, client, detail::loopback_connect_request, succeeded, executor));
        CHECK(succeeded);
        CHECK(client.state() == session_state::closed);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client completes a heartbeat", "[knx][client][integration]")
    {
        bool succeeded {};

        spawn_and_run(detail::complete_heartbeat(client, detail::loopback_connect_request, succeeded, executor));
        CHECK(succeeded);
        CHECK(client.state() == session_state::connected);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client propagates heartbeat failure", "[knx][client][unit]")
    {
        bool failed {};
        bool stayed_connected {};

        spawn_and_run(
            detail::propagate_heartbeat_failure(transport, client, detail::loopback_connect_request, failed, stayed_connected, executor));
        CHECK(failed);
        CHECK(stayed_connected);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client propagates connect failure", "[knx][client][unit]")
    {
        transport.connect_failure = true;
        bool failed {};
        spawn_and_run(detail::propagate_connect_failure(client, detail::loopback_connect_request, failed, executor));
        CHECK(failed);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client retries a timed-out heartbeat", "[knx][client][integration]")
    {
        bool succeeded {};

        spawn_and_run(detail::retry_timed_out_heartbeat(transport, client, detail::loopback_connect_request, succeeded, executor));
        CHECK(succeeded);
        CHECK(client.state() == session_state::connected);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client escalates repeated heartbeat failures",
                     "[knx][client][integration]")
    {
        bool terminal {};

        spawn_and_run(detail::escalate_heartbeat_failures(transport, client, detail::loopback_connect_request, terminal, executor));
        CHECK(terminal);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client rejects operations after shutdown", "[knx][client][unit]")
    {
        const auto& cemi = sample_cemi;

        client.shutdown();
        bool rejected {};

        spawn_and_run(detail::reject_after_shutdown(client, detail::loopback_connect_request, cemi, rejected, executor));
        CHECK(rejected);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client honors task cancellation", "[knx][client][unit]")
    {
        std::stop_source source;
        source.request_stop();
        bool cancelled {};

        auto run = [&]() -> task<void>
        {
            const auto result = co_await client.receive_datagram();
            cancelled = !result.has_value() && result.error() == make_error_code(error::shutdown);
            executor.stop();
        };
        spawn_and_run(std::move(run()).with_stop_token(source.get_token()));
        CHECK(cancelled);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client rejects a concurrent public operation", "[knx][client][unit]")
    {
        transport.hold_receive = true;
        transport.wait_executor = &executor;
        bool first_completed {};
        bool second_rejected {};

        auto first = [&]() -> task<void>
        {
            const auto result = co_await client.receive_datagram();
            first_completed = !result.has_value() && result.error() == make_error_code(error::timeout);
        };
        auto run = [&]() -> task<void>
        {
            // Spawned, not driven: the loop this is running on is the one that will resume it.
            executor.spawn(first());
            const auto result = co_await client.send(sample_cemi);
            second_rejected = !result.has_value() && result.error() == make_error_code(error::send_queue_full);
        };

        spawn_and_run(run());
        CHECK(first_completed);
        CHECK(second_rejected);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client rejects receive APIs after shutdown", "[knx][client][unit]")
    {
        client.shutdown();

        bool rejected {};
        spawn_and_run(detail::reject_receive_after_shutdown(client, rejected, executor));
        CHECK(rejected);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client can reset and reconnect", "[knx][client][integration]")
    {
        bool reconnected {};

        spawn_and_run(detail::reset_and_reconnect(transport, client, detail::loopback_connect_request, reconnected, executor));
        CHECK(reconnected);
        REQUIRE(transport.sent_peers().size() >= 3u);
        const auto& tunnelling_destination =
            reinterpret_cast<const sockaddr_in&>(transport.sent_peers().back());
        CHECK(ntohs(tunnelling_destination.sin_port) == 3673u);
    }

    TEST_CASE("knx tunnelling client exposes lifecycle predicates", "[knx][client][unit]")
    {
        loopback_transport transport;
        sockaddr_storage peer {};
        tunnelling_client client { transport, peer, sizeof(peer) };
        CHECK(!client.connected());
        CHECK(!client.closing());
        CHECK(!client.closed());
        client.shutdown();
        CHECK(!client.connected());
        CHECK(!client.closing());
        CHECK(client.closed());
    }

    TEST_CASE("knx tunnelling client rejects an invalid configured peer", "[knx][client][unit]")
    {
        loopback_transport transport;
        sockaddr_storage peer {};
        tunnelling_client client { transport, peer, 0u };
        const connect_request_frame request {};
        bool rejected {};

        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            const auto result = co_await client.connect(request);
            rejected = !result.has_value() && result.error() == make_error_code(error::invalid_configuration);
            executor.stop();
        };
        executor.spawn(run());
        executor.run();
        CHECK(rejected);
    }

    TEST_CASE("knx tunnelling client rejects a truncated configured IPv4 peer", "[knx][client][unit]")
    {
        loopback_transport transport;
        sockaddr_storage peer {};
        auto& ipv4 = reinterpret_cast<sockaddr_in&>(peer);
        ipv4.sin_family = AF_INET;
        tunnelling_client client { transport, peer, sizeof(sockaddr_in) - 1u };
        bool rejected {};
        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            const auto result = co_await client.connect(connect_request_frame {});
            rejected = !result.has_value() && result.error() == make_error_code(error::invalid_configuration);
            executor.stop();
        };
        executor.spawn(run());
        executor.run();
        CHECK(rejected);
    }

    TEST_CASE("knx tunnelling client preserves transport error codes", "[knx][client][unit]")
    {
        const std::error_code transport_error { EPIPE, std::generic_category() };
        const connect_request_frame& request {detail::loopback_connect_request};

        loopback_transport send_transport;
        send_transport.send_error = transport_error;
        sockaddr_storage send_peer {};
        tunnelling_client send_client { send_transport, send_peer, sizeof(send_peer) };
        completion::executor send_executor;
        bool send_preserved {};
        auto send_run = [&]() -> task<void>
        {
            const auto result = co_await send_client.connect(request);
            send_preserved = !result.has_value() && result.error() == transport_error;
            send_executor.stop();
        };
        send_executor.spawn(send_run());
        send_executor.run();

        loopback_transport receive_transport;
        receive_transport.receive_error = transport_error;
        sockaddr_storage receive_peer {};
        tunnelling_client receive_client { receive_transport, receive_peer, sizeof(receive_peer) };
        completion::executor receive_executor;
        bool receive_preserved {};
        auto receive_run = [&]() -> task<void>
        {
            const auto result = co_await receive_client.receive_datagram();
            receive_preserved = !result.has_value() && result.error() == transport_error;
            receive_executor.stop();
        };
        receive_executor.spawn(receive_run());
        receive_executor.run();

        CHECK(send_preserved);
        CHECK(receive_preserved);
    }

    TEST_CASE("knx connected operations preserve transport error codes", "[knx][client][unit]")
    {
        const std::error_code transport_error { ECONNRESET, std::generic_category() };
        const connect_request_frame& request {detail::loopback_connect_request};
        const auto& cemi = sample_cemi;

        loopback_transport send_transport;
        sockaddr_storage send_peer {};
        tunnelling_client send_client { send_transport, send_peer, sizeof(send_peer) };
        bool send_preserved {};
        completion::executor send_executor;
        send_executor.spawn(
            detail::preserve_send_error(send_transport, send_client, request, cemi, transport_error, send_preserved, send_executor));
        send_executor.run();

        loopback_transport heartbeat_transport;
        sockaddr_storage heartbeat_peer {};
        tunnelling_client heartbeat_client { heartbeat_transport, heartbeat_peer, sizeof(heartbeat_peer) };
        bool heartbeat_preserved {};
        completion::executor heartbeat_executor;
        heartbeat_executor.spawn(detail::preserve_heartbeat_error(heartbeat_transport, heartbeat_client, request, transport_error,
                                                                  heartbeat_preserved, heartbeat_executor));
        heartbeat_executor.run();

        CHECK(send_preserved);
        CHECK(heartbeat_preserved);
    }

    TEST_CASE("knx disconnect preserves transport error codes", "[knx][client][unit]")
    {
        const std::error_code transport_error { ENETUNREACH, std::generic_category() };
        const connect_request_frame& request {detail::loopback_connect_request};

        loopback_transport send_transport;
        sockaddr_storage send_peer {};
        tunnelling_client send_client { send_transport, send_peer, sizeof(send_peer) };
        bool send_preserved {};
        completion::executor send_executor;
        send_executor.spawn(
            detail::preserve_disconnect_send_error(send_transport, send_client, request, transport_error, send_preserved, send_executor));
        send_executor.run();

        loopback_transport receive_transport;
        sockaddr_storage receive_peer {};
        tunnelling_client receive_client { receive_transport, receive_peer, sizeof(receive_peer) };
        bool receive_preserved {};
        completion::executor receive_executor;
        receive_executor.spawn(detail::preserve_disconnect_receive_error(receive_transport, receive_client, request, transport_error,
                                                                         receive_preserved, receive_executor));
        receive_executor.run();

        CHECK(send_preserved);
        CHECK(receive_preserved);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx disconnect rejects an oversized receive count", "[knx][client][unit]")
    {
        bool rejected {};
        spawn_and_run(detail::reject_oversized_disconnect_receive(transport, client, detail::loopback_connect_request, rejected, executor));
        CHECK(rejected);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx disconnect observes cancellation", "[knx][client][unit]")
    {
        std::stop_source source;
        bool cancelled {};
        spawn_and_run(
            std::move(detail::observe_disconnect_cancellation(client, detail::loopback_connect_request, source, cancelled, executor))
                .with_stop_token(source.get_token()));
        CHECK(cancelled);
    }

    TEST_CASE("knx client uses the injected monotonic clock", "[knx][client][integration]")
    {
        test_now_ms = 42'000u;
        loopback_transport transport;
        sockaddr_storage peer {};
        // Distinct values so the assertion below proves which timeout the connect wait uses. A connect is
        // not acknowledged on the tunnelling clock.
        tunnelling_client client {
            transport, peer, sizeof(peer), tunnelling_config {.ack_timeout_ms = 1'000u, .connect_timeout_ms = 7'000u},
            &test_clock_now};
        const connect_request_frame& request {detail::loopback_connect_request};
        bool succeeded {};
        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            succeeded = (co_await client.connect(request)).has_value();
            executor.stop();
        };
        executor.spawn(run());
        executor.run();
        CHECK(succeeded);
        CHECK(client.last_activity_ms() == test_now_ms);
        CHECK(transport.last_receive_deadline == test_now_ms + 7'000u);
        test_now_ms = 0u;
    }

    TEST_CASE("knx client poll closes an inactive session", "[knx][client][integration]")
    {
        test_now_ms = 1u;
        loopback_transport transport;
        sockaddr_storage peer {};
        tunnelling_client client {
            transport,
            peer,
            sizeof(peer),
            tunnelling_config {.inactivity_timeout_ms = 100u},
            &test_clock_now,
        };
        const connect_request_frame& request {detail::loopback_connect_request};
        bool connected {};
        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            connected = (co_await client.connect(request)).has_value();
            executor.stop();
        };
        executor.spawn(run());
        executor.run();
        REQUIRE(connected);
        test_now_ms = 102u;
        const auto expired = client.poll();
        CHECK(!expired.has_value());
        CHECK(expired.error() == make_error_code(error::inactivity_timeout));
        CHECK(client.closed());
        test_now_ms = 0u;
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client rejects an empty cEMI payload", "[knx][client][unit]")
    {
        const std::array<std::uint8_t, 0u> cemi {};
        bool rejected {};
        spawn_and_run(detail::reject_empty_cemi(client, cemi, rejected, executor));
        CHECK(rejected);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client rejects unsupported HPAI connect metadata",
                     "[knx][client][unit]")
    {
        const connect_request_frame request {
            .control_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3671u}, 0x02u},
            .data_endpoint = hpai {ipv4_endpoint {{127u, 0u, 0u, 1u}, 3672u}, 0x01u},
        };
        bool rejected {};
        auto run = [&]() -> task<void>
        {
            const auto result = co_await client.connect(request);
            rejected = !result.has_value() && result.error() == make_error_code(error::unsupported_hpai);
            executor.stop();
        };
        spawn_and_run(run());
        CHECK(rejected);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client rejects zero-port connect metadata", "[knx][client][unit]")
    {
        const auto result_request = connect_request_frame {
            .control_endpoint = hpai { ipv4_endpoint {{127u, 0u, 0u, 1u}, 0u}, 0x01u },
            .data_endpoint = hpai { ipv4_endpoint {{127u, 0u, 0u, 1u}, 3672u}, 0x01u },
        };
        bool rejected {};
        auto run = [&]() -> task<void>
        {
            const auto result = co_await client.connect(result_request);
            rejected = !result.has_value() && result.error() == make_error_code(error::invalid_configuration);
            executor.stop();
        };
        spawn_and_run(run());
        CHECK(rejected);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client rejects an unexpected peer", "[knx][client][unit]")
    {
        transport.wrong_peer = true;
        std::array<std::uint8_t, 10u> packet {};
        REQUIRE(frame::encode_tunnelling_ack_packet(packet, 3u, 7u).has_value());
        transport.enqueue(std::vector<std::uint8_t>(packet.begin(), packet.end()));

        bool rejected {};
        auto run = [&]() -> task<void>
        {
            const auto result = co_await client.receive_datagram();
            rejected = !result.has_value();
            executor.stop();
        };
        spawn_and_run(run());
        CHECK(rejected);
    }

    TEST_CASE("knx tunnelling client connects and sends over IPv6", "[knx][client][integration]")
    {
        loopback_transport transport;
        transport.ipv6_connect = true;
        transport.ipv6_peer = true;
        sockaddr_storage peer {};
        auto& control_peer = reinterpret_cast<sockaddr_in6&>(peer);
        control_peer.sin6_family = AF_INET6;
        control_peer.sin6_port = htons(3671u);
        control_peer.sin6_addr = in6addr_loopback;
        tunnelling_client client { transport, peer, sizeof(sockaddr_in6) };
        const ipv6_connect_request_frame request {
            .control_endpoint = ipv6_hpai {ipv6_endpoint {{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u}, 3671u}, 0x01u},
            .data_endpoint = ipv6_hpai {ipv6_endpoint {{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u}, 3672u}, 0x01u},
        };
        bool succeeded {};
        completion::executor executor;
        executor.spawn(detail::connect_and_send_over_ipv6(client, request, succeeded, executor));
        executor.run();
        CHECK(succeeded);
        REQUIRE(!transport.sent_peers().empty());
        const auto& data_peer = reinterpret_cast<const sockaddr_in6&>(transport.sent_peers().back());
        CHECK(data_peer.sin6_family == AF_INET6);
        CHECK(ntohs(data_peer.sin6_port) == 3672u);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client rejects invalid peer metadata", "[knx][client][unit]")
    {
        transport.invalid_peer_length = true;
        std::array<std::uint8_t, 10u> packet {};
        REQUIRE(frame::encode_tunnelling_ack_packet(packet, 3u, 7u).has_value());
        transport.enqueue(std::vector<std::uint8_t>(packet.begin(), packet.end()));

        bool rejected {};
        auto run = [&]() -> task<void>
        {
            rejected = !(co_await client.receive_datagram()).has_value();
            executor.stop();
        };
        spawn_and_run(run());
        CHECK(rejected);
    }

    TEST_CASE("knx tunnelling client rejects a peer family mismatch", "[knx][client][unit]")
    {
        loopback_transport transport;
        transport.wrong_peer = true;
        sockaddr_storage peer {};
        peer.ss_family = AF_INET;
        tunnelling_client client { transport, peer, sizeof(peer) };
        std::array<std::uint8_t, 10u> packet {};
        REQUIRE(frame::encode_tunnelling_ack_packet(packet, 3u, 7u).has_value());
        transport.enqueue(std::vector<std::uint8_t>(packet.begin(), packet.end()));

        completion::executor executor;
        bool rejected {};
        auto run = [&]() -> task<void>
        {
            rejected = !(co_await client.receive_datagram()).has_value();
            executor.stop();
        };
        executor.spawn(run());
        executor.run();
        CHECK(rejected);
    }

    TEST_CASE("knx tunnelling client accepts a matching IPv6 peer", "[knx][client][unit]")
    {
        loopback_transport transport;
        transport.ipv6_peer = true;
        sockaddr_storage peer {};
        auto& configured = reinterpret_cast<sockaddr_in6&>(peer);
        configured.sin6_family = AF_INET6;
        configured.sin6_port = htons(3672u);
        configured.sin6_addr = in6addr_loopback;
        tunnelling_client client { transport, peer, sizeof(sockaddr_in6) };
        std::array<std::uint8_t, 10u> packet {};
        REQUIRE(frame::encode_tunnelling_ack_packet(packet, 3u, 7u).has_value());
        transport.enqueue(std::vector<std::uint8_t>(packet.begin(), packet.end()));

        bool accepted {};
        completion::executor executor;
        auto run = [&]() -> task<void>
        {
            accepted = (co_await client.receive_datagram()).has_value();
            executor.stop();
        };
        executor.spawn(run());
        executor.run();
        CHECK(accepted);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client rejects tunnelling traffic from the control peer",
                     "[knx][client][unit]")
    {
        transport.data_peer_as_control = true;
        std::array<std::uint8_t, 10u> packet {};
        REQUIRE(frame::encode_tunnelling_ack_packet(packet, 3u, 7u).has_value());
        bool rejected {};
        spawn_and_run(
            detail::reject_control_peer_tunnelling(transport, client, detail::loopback_connect_request, packet, rejected, executor));
        CHECK(rejected);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client rejects a bounded peer-length mismatch", "[knx][client][unit]")
    {
        transport.short_peer_length = true;
        std::array<std::uint8_t, 10u> packet {};
        REQUIRE(frame::encode_tunnelling_ack_packet(packet, 3u, 7u).has_value());
        transport.enqueue(std::vector<std::uint8_t>(packet.begin(), packet.end()));

        bool rejected {};
        auto run = [&]() -> task<void>
        {
            rejected = !(co_await client.receive_datagram()).has_value();
            executor.stop();
        };
        spawn_and_run(run());
        CHECK(rejected);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client rejects malformed application datagrams",
                     "[knx][client][unit]")
    {
        transport.enqueue(std::vector<std::uint8_t> {
            0x06u, 0x10u, 0x04u, 0x21u, 0x00u, 0x05u,
        });

        bool rejected {};
        auto run = [&]() -> task<void>
        {
            const auto result = co_await client.receive_datagram();
            rejected = !result.has_value() && result.error() == make_error_code(error::malformed_frame);
            executor.stop();
        };
        spawn_and_run(run());
        CHECK(rejected);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client rejects an empty datagram", "[knx][client][unit]")
    {
        transport.empty_receive = true;
        bool rejected {};
        auto run = [&]() -> task<void>
        {
            const auto result = co_await client.receive_datagram();
            rejected = !result.has_value() && result.error() == make_error_code(error::invalid_length);
            executor.stop();
        };
        spawn_and_run(run());
        CHECK(rejected);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client rejects an oversized receive result", "[knx][client][unit]")
    {
        transport.oversized_receive = true;
        bool rejected {};
        auto run = [&]() -> task<void>
        {
            const auto result = co_await client.receive_datagram();
            rejected = !result.has_value() && result.error() == make_error_code(error::invalid_length);
            executor.stop();
        };
        spawn_and_run(run());
        CHECK(rejected);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client receives an owning typed datagram",
                     "[knx][client][integration]")
    {
        bool received_ack {};

        std::array<std::uint8_t, 10u> packet {};
        REQUIRE(frame::encode_tunnelling_ack_packet(packet, 3u, 7u).has_value());
        transport.enqueue(std::vector<std::uint8_t>(packet.begin(), packet.end()));

        spawn_and_run(detail::receive_owning_datagram(client, received_ack, executor));
        CHECK(received_ack);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client receives an owning cEMI payload", "[knx][client][integration]")
    {
        const auto& cemi = sample_cemi;
        std::array<std::uint8_t, sample_tunnelling_packet_size> packet {};
        REQUIRE(frame::encode_tunnelling_request_packet(packet, 3u, 7u, cemi).has_value());

        bool matched {};

        spawn_and_run(detail::receive_owning_cemi(transport, client, cemi, packet, matched, executor));
        CHECK(matched);
        CHECK(transport.ack_received);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client returns an owning cEMI copy", "[knx][client][unit]")
    {
        const auto& cemi = sample_cemi;
        std::array<std::uint8_t, sample_tunnelling_packet_size> request {};
        REQUIRE(frame::encode_tunnelling_request_packet(request, 3u, 7u, cemi).has_value());
        std::vector<std::uint8_t> received_payload {};

        spawn_and_run(detail::receive_owning_cemi_copy(transport, client, request, received_payload, executor));
        CHECK(received_payload == std::vector<std::uint8_t>(cemi.begin(), cemi.end()));
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client acknowledges but suppresses duplicate indications",
                     "[knx][client][integration]")
    {
        std::array<std::uint8_t, sample_tunnelling_packet_size> indication {};
        REQUIRE(frame::encode_tunnelling_request_packet(indication, 3u, 7u, sample_cemi).has_value());

        bool first_received {};
        bool duplicate_suppressed {};
        spawn_and_run(detail::suppress_duplicate_indication(transport, client, indication, first_received, duplicate_suppressed, executor));
        CHECK(first_received);
        CHECK(duplicate_suppressed);
        CHECK(transport.ack_received);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client rejects out-of-order indications",
                     "[knx][client][integration]")
    {
        std::array<std::uint8_t, sample_tunnelling_packet_size> first {};
        std::array<std::uint8_t, sample_tunnelling_packet_size> out_of_order {};
        REQUIRE(frame::encode_tunnelling_request_packet(first, 3u, 7u, sample_cemi).has_value());
        REQUIRE(frame::encode_tunnelling_request_packet(out_of_order, 3u, 9u, sample_cemi).has_value());

        bool rejected {};
        spawn_and_run(detail::reject_out_of_order_indication(transport, client, first, out_of_order, rejected, executor));
        CHECK(rejected);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client skips control datagrams before cEMI",
                     "[knx][client][integration]")
    {
        const auto& cemi = sample_cemi;
        std::array<std::uint8_t, sample_tunnelling_packet_size> request {};
        REQUIRE(frame::encode_tunnelling_request_packet(request, 3u, 7u, cemi).has_value());
        std::array<std::uint8_t, 10u> ack {};
        REQUIRE(frame::encode_tunnelling_ack_packet(ack, 3u, 7u).has_value());
        bool received {};
        spawn_and_run(detail::skip_control_before_cemi(transport, client, request, ack, received, executor));
        CHECK(received);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client processes heartbeat before cEMI", "[knx][client][integration]")
    {
        std::array<std::uint8_t, sample_tunnelling_packet_size> request {};
        const auto& cemi = sample_cemi;
        REQUIRE(frame::encode_tunnelling_request_packet(request, 3u, 7u, cemi).has_value());

        bool received {};
        spawn_and_run(detail::process_heartbeat_before_cemi(transport, client, request, received, executor));
        CHECK(received);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client rejects cross-channel cEMI", "[knx][client][unit]")
    {
        const auto& cemi = sample_cemi;
        std::array<std::uint8_t, sample_tunnelling_packet_size> request {};
        REQUIRE(frame::encode_tunnelling_request_packet(request, 4u, 7u, cemi).has_value());

        bool rejected {};
        spawn_and_run(detail::reject_cross_channel_cemi(transport, client, request, rejected, executor));
        CHECK(rejected);
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client does not expose disconnect control as cEMI",
                     "[knx][client][unit]")
    {
        const std::array<std::uint8_t, 16u> disconnect {
            0x06u, 0x10u, 0x02u, 0x09u, 0x00u, 0x10u, 0x03u, 0x00u, 0x08u, 0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        };
        bool rejected {};
        spawn_and_run(detail::reject_disconnect_control_as_cemi(transport, client, disconnect, rejected, executor));
        CHECK(rejected);
    }
}

namespace kmx::aio::test::knx::client_test
{
    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client writes a typed group value", "[knx][client][dpt][integration]")
    {
        bool succeeded {};

        spawn_and_run(detail::write_typed_group_value(client, succeeded, executor));

        REQUIRE(succeeded);
        CHECK(client.assigned_address() == individual_address {1u, 1u, 10u});

        // The interface substitutes its own source address, so the client sends an unset one; every other
        // octet must match the golden switch-on telegram.
        auto expected = sample_cemi;
        expected[4u] = 0u;
        expected[5u] = 0u;
        CHECK(detail::last_sent_cemi(transport) == std::vector<std::uint8_t>(expected.begin(), expected.end()));
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client reads a group value", "[knx][client][dpt][integration]")
    {
        bool succeeded {};

        spawn_and_run(detail::read_group_value(client, succeeded, executor));

        REQUIRE(succeeded);
        const auto sent = detail::last_sent_cemi(transport);
        const auto decoded = cemi::decode(sent);
        REQUIRE(decoded.has_value());
        CHECK(decoded->application_service == apci::group_value_read);
        CHECK(decoded->group_destination() == group_address {0x0A03u});
    }

    TEST_CASE_METHOD(detail::loopback_client_fixture, "knx tunnelling client decodes a received telegram",
                     "[knx][client][dpt][integration]")
    {
        std::expected<telegram, std::error_code> received {std::unexpected(make_error_code(error::internal_error))};

        std::vector<std::uint8_t> indication(sample_tunnelling_packet_size + 2u, 0u);
        REQUIRE(frame::encode_tunnelling_request_packet(indication, 3u, 0u, sample_cemi_temperature).has_value());

        spawn_and_run(detail::decode_received_telegram(transport, client, indication, received, executor));

        REQUIRE(received.has_value());
        CHECK(received->frame.application_service == apci::group_value_write);
        CHECK(received->frame.group_destination() == group_address {0x0A03u});
        CHECK(received->payload().size() == 2u);
        CHECK(received->value_as<9u>().value() == 21.5f);
    }
}
