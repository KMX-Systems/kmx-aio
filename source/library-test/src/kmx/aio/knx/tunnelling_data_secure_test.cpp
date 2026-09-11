/// @file src/kmx/aio/knx/tunnelling_data_secure_test.cpp
/// @brief KNX Data Secure end to end through a tunnel: the in-tree client secures, the in-tree server passes, a device opens.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details The server does not apply Data Secure, as a gateway does not: it hands the secured APDU to its handler untouched.
/// The handler plays the bus device, with a Data Secure context of its own. It opens the client's switch-on and answers
/// with a secured switch-off, which the client opens before handing it over.
#ifndef PCH
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/completion/knx/tcp_server.hpp>
    #include <kmx/aio/completion/knx/tcp_transport.hpp>
    #include <kmx/aio/completion/timer.hpp>
    #include <kmx/aio/knx/apdu_payload.hpp>
    #include <kmx/aio/knx/cemi.hpp>
    #include <kmx/aio/knx/cemi_frame.hpp>
    #include <kmx/aio/knx/data_secure.hpp>
    #include <kmx/aio/knx/data_secure/context.hpp>
    #include <kmx/aio/knx/data_secure/sequence_store.hpp>
    #include <kmx/aio/knx/dpt.hpp>
    #include <kmx/aio/knx/generic_server.hpp>
    #include <kmx/aio/knx/keyring/document.hpp>
    #include <kmx/aio/knx/server.hpp>
    #include <kmx/aio/knx/telegram.hpp>
    #include <kmx/aio/knx/tunnelling_client.hpp>
    #include <kmx/aio/test/knx/secure_vectors.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <array>
    #include <atomic>
    #include <chrono>
    #include <cstdint>
    #include <vector>
    #include <netinet/in.h>
#endif

namespace kmx::aio::test::knx::tunnelling_data_secure_test
{
    namespace kn = kmx::aio::knx;
    namespace ds = kmx::aio::knx::data_secure;
    namespace kr = kmx::aio::knx::keyring;
    namespace sv = kmx::aio::test::knx::secure_vectors;

    namespace detail
    {
        constexpr std::chrono::milliseconds patience {5'000};
        constexpr std::string_view group_key = "d2effbdc88eb5c9a57a73b0aad64996f";
        /// @brief The group both ends secure.
        constexpr std::uint16_t group = 0x0A03u;
        /// @brief The address the server gives the tunnel, and the bus device's own.
        const kn::individual_address tunnel_address {1u, 1u, 50u};
        const kn::individual_address device_address {1u, 1u, 1u};

        /// @brief Starts its sequence numbers where it is told to.
        class fixed_store final: public ds::sequence_store
        {
        public:
            explicit fixed_store(const std::uint64_t first) noexcept: first_(first) {}

            [[nodiscard]] std::expected<std::uint64_t, std::error_code> load() noexcept override { return first_; }
            [[nodiscard]] expected_void_t reserve_until(const std::uint64_t limit) noexcept override
            {
                first_ = limit;
                return {};
            }

        private:
            std::uint64_t first_ {};
        };

        /// @brief A configuration keying the group, trusting @p sender.
        [[nodiscard]] ds::configuration configuration(const kn::individual_address sender)
        {
            ds::configuration value {};
            value.group_keys.push_back(kr::group_key {.address = kn::group_address {group}, .key = sv::key(sv::hex(group_key))});
            value.senders.push_back(ds::sender_sequence {.address = sender, .last_valid_sequence = 0u});
            return value;
        }

        /// @brief What each end saw.
        struct observation
        {
            bool connected {};
            bool written {};
            std::atomic_bool secured_on_the_wire {};
            std::atomic_bool device_opened {};
            std::atomic_bool answered {};
            bool client_opened {};
            bool disconnected {};
        };

        [[nodiscard]] sockaddr_in loopback(const port_t port) noexcept
        {
            sockaddr_in address {};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons(port);
            return address;
        }

        /// @brief Indicates whether a cEMI frame carries A_SecureService.
        [[nodiscard]] bool protected_telegram(const cspan_uint8_t cemi) noexcept
        {
            const auto frame = kn::cemi::decode(cemi);
            return frame.has_value() && (frame->application_service == kn::apci::secure_service);
        }

        /// @brief The bus device: opens what the client tunnelled in, and answers it with a secured switch-off.
        [[nodiscard]] kn::server_event_handler device(kn::generic_server& server, ds::context& context, observation& observed)
        {
            return [&server, &context, &observed](kn::server_event event) -> task<void>
            {
                observed.secured_on_the_wire = protected_telegram(event.cemi_bytes);
                const auto opened = context.open_frame(event.cemi_bytes);
                const auto frame = opened.has_value() ? kn::cemi::decode(*opened) : std::unexpected(kn::error::malformed_frame);
                observed.device_opened = frame.has_value() && (frame->application_service == kn::apci::group_value_write) &&
                                         (frame->compact_value == 1u) && (frame->source == tunnel_address);
                std::array<std::uint8_t, kn::cemi::max_l_data_size> answer {};
                const auto size = kn::cemi::encode(answer, {.code = kn::cemi_message_code::l_data_ind,
                                                            .source = device_address,
                                                            .destination = kn::group_address {group}.value(),
                                                            .service = kn::apci::group_value_write,
                                                            .payload = kn::apdu_payload::compact(0u)});
                if (!size.has_value())
                    co_return;
                const auto secured = context.secure_frame({answer.data(), *size});
                if (secured.has_value())
                    observed.answered = (co_await server.send(event.channel_id, *secured)).has_value();
            };
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

        task<void> run_server(completion::knx::tcp_server& tcp, std::atomic_bool& ended)
        {
            static_cast<void>(co_await tcp.serve());
            ended = true;
        }
    }

    TEST_CASE("knx tunnelling client exchanges Data Secure group telegrams through the in-tree server",
              "[knx][data_secure][tcp][integration][completion]")
    {
        completion::executor executor;
        kn::generic_server server {kn::server_config {.first_assigned_address = detail::tunnel_address}};
        detail::fixed_store device_store {1'000u};
        ds::context device_context {detail::configuration(detail::tunnel_address), &device_store};
        detail::observation observed {};
        completion::knx::tcp_server tcp {
            executor,
            server,
            {.bind_address = {127u, 0u, 0u, 1u}, .port = 0u, .on_event = detail::device(server, device_context, observed)}};
        REQUIRE(tcp.listen().has_value());
        const auto address = detail::loopback(tcp.port());
        completion::knx::tcp_transport transport {executor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)};
        kn::tunnelling_client client {transport, reinterpret_cast<const sockaddr*>(&address), sizeof(address)};
        detail::fixed_store client_store {1u};
        ds::context client_context {detail::configuration(detail::device_address), &client_store};
        client.use_data_secure(&client_context);
        std::atomic_bool serving_ended {};

        auto run = [&]() -> task<void>
        {
            executor.spawn(detail::run_server(tcp, serving_ended));
            observed.connected = (co_await client.connect(kn::connect_request_frame {})).has_value();
            const auto switch_on = kn::dpt::encode<1u>(true);
            observed.written = observed.connected && switch_on.has_value() &&
                               (co_await client.write_group_value(kn::group_address {detail::group}, *switch_on)).has_value();
            const auto telegram = observed.written ? co_await client.receive_telegram() : std::unexpected(std::error_code {});
            observed.client_opened = telegram.has_value() && (telegram->frame.application_service == kn::apci::group_value_write) &&
                                     (telegram->frame.compact_value == 0u) && (telegram->frame.source == detail::device_address);
            observed.disconnected = (co_await client.disconnect()).has_value();
            static_cast<void>(co_await detail::settle(executor, [&]() { return tcp.connections() == 0u; }));
            tcp.stop();
            static_cast<void>(co_await detail::settle(executor, [&]() { return serving_ended.load(); }));
            executor.stop();
        };
        executor.spawn(run());
        executor.run();

        CHECK(observed.connected);
        CHECK(observed.written);
        CHECK(observed.secured_on_the_wire.load());
        CHECK(observed.device_opened.load());
        CHECK(observed.answered.load());
        CHECK(observed.client_opened);
        CHECK(observed.disconnected);
        CHECK(device_context.counters().authentication_failures == 0u);
        CHECK(client_context.counters().authentication_failures == 0u);
    }
}
