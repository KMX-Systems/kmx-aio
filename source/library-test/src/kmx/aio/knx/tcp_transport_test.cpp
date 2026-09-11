/// @file src/kmx/aio/knx/tcp_transport_test.cpp
/// @brief The KNX TCP transports on real loopback connections, on both executor pillars.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details A blocking server on its own thread writes KNXnet/IP frames cut at awkward places - inside a header,
/// inside a body, two frames at once - stalls in the middle of one, and closes in the middle of another. The
/// transports must hand each frame over whole, lose nothing to a deadline, and tell a clean close from a cut one.
#ifndef PCH
    #include <kmx/aio/completion/knx/tcp_transport.hpp>

    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/file_descriptor.hpp>
    #include <kmx/aio/knx/connection.hpp>
    #include <kmx/aio/knx/frame.hpp>
    #include <kmx/aio/test/knx/telegram.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <array>
    #include <chrono>
    #include <cstdint>
    #include <memory>
    #include <span>
    #include <system_error>
    #include <thread>
    #include <vector>
    #include <netinet/in.h>
    #include <poll.h>
    #include <sys/socket.h>

    #if defined(KMX_AIO_FEATURE_READINESS)
        #include <kmx/aio/readiness/executor.hpp>
        #include <kmx/aio/readiness/knx/tcp_transport.hpp>
        #include <kmx/aio/readiness/timer.hpp>
    #endif
#endif

namespace kmx::aio::test::knx::tcp_transport_test
{
    using namespace kmx::aio::knx;

    namespace detail
    {
        /// @brief How long the stand-in server waits on the client, and a client receive on the server.
        constexpr std::uint32_t patience_ms = 5'000u;

        /// @brief A frame sequence, as the octets of each frame.
        using frames_t = std::vector<std::vector<std::uint8_t>>;

        /// @brief A listening loopback socket and the address it is bound to.
        struct loopback_listener
        {
            file_descriptor socket {};
            sockaddr_in address {};
        };

        /// @brief What the stand-in server did and saw.
        struct server_observation
        {
            bool accepted {};
            bool written {};
            std::vector<std::uint8_t> echoed {};
        };

        /// @brief What the client side of a framing exchange saw.
        struct client_observation
        {
            bool opened {};
            frames_t frames {};
            ::socklen_t peer_length {};
            bool sent {};
            std::error_code end {};
        };

        /// @brief What the client side of a stalled exchange saw.
        struct stall_observation
        {
            bool opened {};
            std::error_code early {};
            std::vector<std::uint8_t> frame {};
            std::error_code truncated {};
        };

        [[nodiscard]] std::uint32_t now_ms() noexcept
        {
            const auto now = std::chrono::steady_clock::now().time_since_epoch();
            return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
        }

        template <typename Value>
        [[nodiscard]] std::error_code error_of(const expected_t<Value>& result) noexcept
        {
            return result.has_value() ? std::error_code {} : result.error();
        }

        [[nodiscard]] loopback_listener listen_loopback()
        {
            loopback_listener listener {};
            listener.socket = file_descriptor {::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
            REQUIRE(listener.socket.is_valid());
            listener.address.sin_family = AF_INET;
            listener.address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            REQUIRE(::bind(listener.socket.get(), reinterpret_cast<const sockaddr*>(&listener.address), sizeof(listener.address)) == 0);
            REQUIRE(::listen(listener.socket.get(), 1) == 0);
            ::socklen_t length = sizeof(listener.address);
            REQUIRE(::getsockname(listener.socket.get(), reinterpret_cast<sockaddr*>(&listener.address), &length) == 0);
            return listener;
        }

        /// @brief Returns a loopback address nothing listens on.
        [[nodiscard]] sockaddr_in unused_loopback_address()
        {
            return listen_loopback().address;
        }

        [[nodiscard]] file_descriptor accept_one(const loopback_listener& listener) noexcept
        {
            ::pollfd waiting {listener.socket.get(), POLLIN, 0};
            if (::poll(&waiting, 1u, static_cast<int>(patience_ms)) != 1)
                return file_descriptor {};
            return file_descriptor {::accept4(listener.socket.get(), nullptr, nullptr, SOCK_CLOEXEC)};
        }

        [[nodiscard]] bool write_all(const file_descriptor& connection, std::span<const std::uint8_t> octets) noexcept
        {
            while (!octets.empty())
            {
                const auto written = ::send(connection.get(), octets.data(), octets.size(), MSG_NOSIGNAL);
                if (written <= 0)
                    return false;
                octets = octets.subspan(static_cast<std::size_t>(written));
            }

            return true;
        }

        [[nodiscard]] std::vector<std::uint8_t> read_exactly(const file_descriptor& connection, const std::size_t count)
        {
            std::vector<std::uint8_t> octets(count, 0u);
            for (std::size_t filled {}; filled < count;)
            {
                ::pollfd waiting {connection.get(), POLLIN, 0};
                if (::poll(&waiting, 1u, static_cast<int>(patience_ms)) != 1)
                    return {};
                const auto received = ::recv(connection.get(), octets.data() + filled, count - filled, 0);
                if (received <= 0)
                    return {};
                filled += static_cast<std::size_t>(received);
            }

            return octets;
        }

        /// @brief Gives the client time to read what was just written, so the next write arrives separately.
        void let_the_client_read()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds {20});
        }

        [[nodiscard]] std::vector<std::uint8_t> tunnelling_frame(const std::uint8_t sequence)
        {
            std::vector<std::uint8_t> packet(sample_tunnelling_packet_size, 0u);
            REQUIRE(frame::encode_tunnelling_request_packet(packet, 7u, sequence, sample_cemi).has_value());
            return packet;
        }

        [[nodiscard]] std::vector<std::uint8_t> heartbeat_frame()
        {
            std::vector<std::uint8_t> packet(frame::communication_header_size + connection::control_request_body_size, 0u);
            REQUIRE(connection::encode_connectionstate_request_packet(packet, connectionstate_request_frame {7u}).has_value());
            return packet;
        }

        /// @brief Tunnelling requests and heartbeats in turn: two frame lengths, so every cut lands somewhere new.
        [[nodiscard]] frames_t sample_frames()
        {
            return {tunnelling_frame(0u), heartbeat_frame(), tunnelling_frame(1u), heartbeat_frame(), tunnelling_frame(2u)};
        }

        [[nodiscard]] std::vector<std::uint8_t> joined(const frames_t& frames)
        {
            std::vector<std::uint8_t> stream {};
            for (const auto& octets: frames)
                stream.insert(stream.end(), octets.begin(), octets.end());
            return stream;
        }

        /// @brief Writes @p frames cut short inside a header and inside a body, with two whole frames in the last
        ///        write, then reads back @p echo_size octets and closes the connection.
        void serve_framing(const loopback_listener& listener, const frames_t& frames, const std::size_t echo_size,
                           server_observation& observed)
        {
            const auto connection = accept_one(listener);
            observed.accepted = connection.is_valid();
            if (!observed.accepted)
                return;

            const auto stream = joined(frames);
            const std::span<const std::uint8_t> octets {stream};
            const auto first = frames[0u].size();
            const auto second = frames[1u].size();
            const std::array<std::size_t, 4u> cuts {3u, first + second - 1u, first + second + 5u, stream.size()};
            observed.written = true;
            for (std::size_t begin {}; const auto end: cuts)
            {
                observed.written = write_all(connection, octets.subspan(begin, end - begin)) && observed.written;
                begin = end;
                let_the_client_read();
            }

            observed.echoed = read_exactly(connection, echo_size);
        }

        /// @brief Writes the front of @p whole, stalls past the client's deadline, finishes it, then closes the
        ///        connection four octets into @p cut_short.
        void serve_stall(const loopback_listener& listener, const std::vector<std::uint8_t>& whole,
                         const std::vector<std::uint8_t>& cut_short, server_observation& observed)
        {
            const auto connection = accept_one(listener);
            observed.accepted = connection.is_valid();
            if (!observed.accepted)
                return;

            const std::span<const std::uint8_t> octets {whole};
            observed.written = write_all(connection, octets.first(10u));
            std::this_thread::sleep_for(std::chrono::milliseconds {150});
            observed.written = write_all(connection, octets.subspan(10u)) && observed.written;
            observed.written = write_all(connection, std::span<const std::uint8_t> {cut_short}.first(4u)) && observed.written;
            let_the_client_read();
        }

        /// @brief How many frames a client expects the server to write, and the frames it sends back.
        struct exchange_plan
        {
            /// @brief How many frames the server writes.
            std::size_t expected {};
            /// @brief The frames sent back once they have arrived.
            frames_t echo {};
        };

        /// @brief Receives every frame the server writes, sends the planned echo back, and waits for the server to close.
        template <typename Executor, typename Transport>
        task<void> exchange_frames(Executor& executor, Transport& transport, const exchange_plan plan, client_observation& observed)
        {
            observed.opened = (co_await transport.open()).has_value();
            std::array<std::byte, frame::max_datagram_size> buffer {};
            transport_peer peer {};
            for (std::size_t count {}; observed.opened && (count < plan.expected); ++count)
            {
                const auto received = co_await transport.receive_until(buffer, peer, now_ms() + patience_ms);
                if (!received.has_value())
                    break;
                const auto* octets = reinterpret_cast<const std::uint8_t*>(buffer.data());
                observed.frames.emplace_back(octets, octets + *received);
                observed.peer_length = peer.length;
            }

            observed.sent = observed.opened;
            for (const auto& octets: plan.echo)
            {
                const auto sent = co_await transport.send({reinterpret_cast<const std::byte*>(octets.data()), octets.size()}, nullptr, 0u);
                observed.sent = sent.has_value() && observed.sent;
            }

            observed.end = error_of(co_await transport.receive_until(buffer, peer, now_ms() + patience_ms));
            executor.stop();
        }

        /// @brief Gives up on a frame at a short deadline, then receives it whole, then meets the cut-short one.
        template <typename Executor, typename Transport>
        task<void> outlast_a_stall(Executor& executor, Transport& transport, stall_observation& observed)
        {
            observed.opened = (co_await transport.open()).has_value();
            std::array<std::byte, frame::max_datagram_size> buffer {};
            transport_peer peer {};
            observed.early = error_of(co_await transport.receive_until(buffer, peer, now_ms() + 40u));
            if (const auto rest = co_await transport.receive_until(buffer, peer, now_ms() + patience_ms); rest.has_value())
            {
                const auto* octets = reinterpret_cast<const std::uint8_t*>(buffer.data());
                observed.frame.assign(octets, octets + *rest);
            }

            observed.truncated = error_of(co_await transport.receive_until(buffer, peer, now_ms() + patience_ms));
            executor.stop();
        }

        /// @brief Nothing to wait for: the completion executor resumes a task that waits on I/O only from the loop run()
        ///        drives, so its stop() always lands while run() is under way.
        task<void> await_the_loop(completion::executor&)
        {
            co_return;
        }

#if defined(KMX_AIO_FEATURE_READINESS)
        /// @brief Suspends until the readiness executor's I/O loop resumes this task, which it does only once run() is
        ///        under way.
        /// @details The readiness executor starts a spawned task on a scheduler worker at once. A refused loopback connect
        ///          can finish without ever suspending, and a stop() made before run() begins is lost: run() then waits
        ///          for a stop that has already been and gone.
        task<void> await_the_loop(readiness::executor& executor)
        {
            auto timer = readiness::timer::create();
            if (!timer.has_value() || !executor.register_fd(timer->get()).has_value())
                co_return;
            ::itimerspec when {};
            when.it_value.tv_nsec = 1'000'000;
            if (timer->set_time(0, when).has_value())
                static_cast<void>(co_await timer->wait(executor));
            executor.unregister_fd(timer->get());
        }
#endif

        template <typename Executor, typename Transport>
        task<void> open_refused(Executor& executor, Transport& transport, std::error_code& failure)
        {
            co_await await_the_loop(executor);
            failure = error_of(co_await transport.open());
            executor.stop();
        }

        void check_exchange(const frames_t& frames, const frames_t& echo, const server_observation& server,
                            const client_observation& client)
        {
            CHECK(server.accepted);
            CHECK(server.written);
            CHECK(client.opened);
            CHECK(client.frames == frames);
            CHECK(client.peer_length == sizeof(sockaddr_in));
            CHECK(client.sent);
            CHECK(server.echoed == joined(echo));
            // The server closed between frames, which is the end of the connection rather than a damaged frame.
            CHECK(client.end == make_error_code(error::shutdown));
        }

        void check_stall(const std::vector<std::uint8_t>& whole, const server_observation& server, const stall_observation& client)
        {
            CHECK(server.accepted);
            CHECK(server.written);
            CHECK(client.opened);
            CHECK(client.early == make_error_code(error::timeout));
            // The octets that arrived before the deadline were kept, not dropped with it.
            CHECK(client.frame == whole);
            CHECK(client.truncated == make_error_code(error::malformed_frame));
        }
    }

    TEST_CASE("knx completion tcp transport recovers frames however the stream cuts them", "[knx][tcp][integration][completion]")
    {
        const auto frames = detail::sample_frames();
        const detail::frames_t echo {frames[0u], frames[1u]};
        const auto listener = detail::listen_loopback();
        detail::server_observation server_side {};
        std::jthread server([&]() { detail::serve_framing(listener, frames, detail::joined(echo).size(), server_side); });

        completion::executor executor;
        completion::knx::tcp_transport transport {executor, reinterpret_cast<const sockaddr*>(&listener.address), sizeof(listener.address)};
        detail::client_observation client_side {};
        executor.spawn(detail::exchange_frames(executor, transport, {frames.size(), echo}, client_side));
        executor.run();
        server.join();
        detail::check_exchange(frames, echo, server_side, client_side);
    }

    TEST_CASE("knx completion tcp transport keeps a frame across a deadline and spots a cut one", "[knx][tcp][integration][completion]")
    {
        const auto whole = detail::tunnelling_frame(3u);
        const auto cut_short = detail::tunnelling_frame(4u);
        const auto listener = detail::listen_loopback();
        detail::server_observation server_side {};
        std::jthread server([&]() { detail::serve_stall(listener, whole, cut_short, server_side); });

        completion::executor executor;
        completion::knx::tcp_transport transport {executor, reinterpret_cast<const sockaddr*>(&listener.address), sizeof(listener.address)};
        detail::stall_observation client_side {};
        executor.spawn(detail::outlast_a_stall(executor, transport, client_side));
        executor.run();
        server.join();
        detail::check_stall(whole, server_side, client_side);
    }

    TEST_CASE("knx completion tcp transport reports a refused connection", "[knx][tcp][integration][completion]")
    {
        const auto address = detail::unused_loopback_address();
        completion::executor executor;
        completion::knx::tcp_transport transport {executor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)};
        std::error_code failure {};
        executor.spawn(detail::open_refused(executor, transport, failure));
        executor.run();
        CHECK(failure == make_error_code(error::connection_failed));
        CHECK(!transport.is_open());
    }

#if defined(KMX_AIO_FEATURE_READINESS)
    TEST_CASE("knx readiness tcp transport recovers frames however the stream cuts them", "[knx][tcp][integration][readiness]")
    {
        const auto frames = detail::sample_frames();
        const detail::frames_t echo {frames[0u], frames[1u]};
        const auto listener = detail::listen_loopback();
        detail::server_observation server_side {};
        std::jthread server([&]() { detail::serve_framing(listener, frames, detail::joined(echo).size(), server_side); });

        auto executor = std::make_shared<readiness::executor>(readiness::executor_config {.thread_count = 1u, .timeout_ms = 20u});
        readiness::knx::tcp_transport transport {*executor, reinterpret_cast<const sockaddr*>(&listener.address), sizeof(listener.address)};
        detail::client_observation client_side {};
        executor->spawn(detail::exchange_frames(*executor, transport, {frames.size(), echo}, client_side));
        std::jthread runner([executor]() { executor->run(); });
        runner.join();
        server.join();
        detail::check_exchange(frames, echo, server_side, client_side);
    }

    TEST_CASE("knx readiness tcp transport keeps a frame across a deadline and spots a cut one", "[knx][tcp][integration][readiness]")
    {
        const auto whole = detail::tunnelling_frame(3u);
        const auto cut_short = detail::tunnelling_frame(4u);
        const auto listener = detail::listen_loopback();
        detail::server_observation server_side {};
        std::jthread server([&]() { detail::serve_stall(listener, whole, cut_short, server_side); });

        auto executor = std::make_shared<readiness::executor>(readiness::executor_config {.thread_count = 1u, .timeout_ms = 20u});
        readiness::knx::tcp_transport transport {*executor, reinterpret_cast<const sockaddr*>(&listener.address), sizeof(listener.address)};
        detail::stall_observation client_side {};
        executor->spawn(detail::outlast_a_stall(*executor, transport, client_side));
        std::jthread runner([executor]() { executor->run(); });
        runner.join();
        server.join();
        detail::check_stall(whole, server_side, client_side);
    }

    TEST_CASE("knx readiness tcp transport reports a refused connection", "[knx][tcp][integration][readiness]")
    {
        const auto address = detail::unused_loopback_address();
        auto executor = std::make_shared<readiness::executor>(readiness::executor_config {.thread_count = 1u, .timeout_ms = 20u});
        readiness::knx::tcp_transport transport {*executor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)};
        std::error_code failure {};
        executor->spawn(detail::open_refused(*executor, transport, failure));
        std::jthread runner([executor]() { executor->run(); });
        runner.join();
        CHECK(failure == make_error_code(error::connection_failed));
        CHECK(!transport.is_open());
    }
#endif
}
