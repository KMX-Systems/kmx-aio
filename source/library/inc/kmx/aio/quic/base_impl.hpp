/// @file inc/kmx/aio/quic/base_impl.hpp
/// @brief Common QUIC engine implementation template shared between the readiness and completion models.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details This is a PRIVATE implementation detail — included only from the .cpp files.
///          It must NOT appear in any public header to avoid exposing lsquic.h to consumers.
#pragma once
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/quic/base_engine.hpp>
    #include <kmx/aio/quic/engine.hpp>
    #include <kmx/aio/quic/primary_base_impl.hpp>
    #include <kmx/aio/quic/stream_payload.hpp>
    #include <kmx/aio/readiness/descriptor/timer.hpp>
    #include <kmx/aio/task.hpp>

    #include <lsquic.h>

    #include <cstdint>
    #include <expected>
    #include <memory>
    #include <optional>
    #include <system_error>
    #include <utility>
    #include <sys/socket.h>
    #include <sys/uio.h>
    #include <time.h>
#endif

namespace kmx::aio::quic
{
    /// @brief Common QUIC engine implementation shared between readiness and completion models.
    /// @details Adds to @ref primary_base_impl only what depends on the executor and socket types: socket ownership,
    ///          coroutine spawning, the idle-tick strategies, and the event loop.
    /// @tparam Executor  The executor type (readiness::executor or completion::executor).
    /// @tparam UdpSocket The UDP socket type (readiness::udp::socket or completion::udp::socket).
    template <typename Executor, typename UdpSocket>
    struct base_impl: primary_base_impl
    {
        /// @brief The executor driving this engine's I/O.
        Executor& exec_;
        /// @brief The bound UDP socket carrying all QUIC datagrams.
        std::unique_ptr<UdpSocket> socket_;

        /// @brief Constructs an engine bound to an executor.
        /// @param exec The executor that will drive the engine's socket and timers.
        explicit base_impl(Executor& exec) noexcept: primary_base_impl(&spawn_stream_task), exec_(exec) {}

        /// @brief Destroys the lsquic engine while the UDP socket is still open.
        ~base_impl() noexcept { destroy_lsquic_engine(); }

        /// @brief Shared initialisation logic called after model-specific socket creation.
        /// @param sock_res The freshly created UDP socket, or the error that creating it produced.
        /// @param params   Where to bind, the borrowed `SSL_CTX` and the QUIC settings.
        /// @return Success, or an error code if the socket, bind or engine step failed.
        [[nodiscard]] expected_void_t setup(std::expected<UdpSocket, std::error_code>&& sock_res, const start_params& params)
        {
            if (auto adopt_res = adopt_socket(std::move(sock_res)); !adopt_res)
                return std::unexpected(adopt_res.error());

            return setup_after_socket(params);
        }

        /// @brief Prepares a client engine that sends each payload on its own stream once the handshake completes.
        /// @param sock_res The freshly created UDP socket, or the error that creating it produced.
        /// @param params   The server to connect to, the SNI hostname, the payloads (empty entries are skipped), the
        ///                 borrowed `SSL_CTX` and the QUIC settings.
        /// @return Success, or an error code if the socket, bind, engine, or connect step failed.
        [[nodiscard]] expected_void_t connect_setup(std::expected<UdpSocket, std::error_code>&& sock_res, const connect_params& params)
        {
            set_client_payloads(params.payloads);
            if (auto adopt_res = adopt_socket(std::move(sock_res)); !adopt_res)
                return std::unexpected(adopt_res.error());

            return connect_setup_after_socket(params);
        }

    private:
        /// @brief The @ref primary_base_impl::spawn_stream_task_ thunk: resolves the executor and spawns the handler.
        /// @param self    The owning engine, always a @ref base_impl.
        /// @param stream  The stream the payload arrived on.
        /// @param payload The received payload.
        static void spawn_stream_task(primary_base_impl& self, ::lsquic_stream_t* const stream, stream_payload payload)
        {
            auto& impl = static_cast<base_impl&>(self);
            impl.exec_.spawn(impl.stream_handler_(stream, std::move(payload)));
        }

        /// @brief Takes ownership of a freshly created socket and publishes its descriptor to @ref socket_fd_.
        /// @param sock_res The freshly created UDP socket, or the error that creating it produced.
        /// @return Success, or the socket creation error.
        [[nodiscard]] expected_void_t adopt_socket(std::expected<UdpSocket, std::error_code>&& sock_res)
        {
            if (!sock_res)
                return std::unexpected(sock_res.error());

            socket_ = std::make_unique<UdpSocket>(std::move(*sock_res));
            socket_fd_ = socket_->get_fd();
            return {};
        }

        /// @brief Scope guard that unregisters the readiness watchdog timer when @ref process returns.
        struct timer_guard_t
        {
            /// @brief The executor the timer descriptor is registered with.
            Executor& exec;
            /// @brief The watchdog timer to unregister; empty in the completion model.
            std::optional<kmx::aio::readiness::descriptor::timer>& tick;

            /// @brief Unregisters the watchdog timer if one was created.
            ~timer_guard_t() noexcept
            {
                if (tick && tick->is_valid())
                    if constexpr (requires(Executor& e) { e.unregister_fd(0); })
                        exec.unregister_fd(tick->get());
            }
        };

        /// @brief Creates and registers the readiness watchdog timer, if the executor lacks a native timeout.
        /// @param readiness_tick Receives the created timer; left empty for completion-model executors.
        /// @return Success, or an error code if the timer could not be created or registered.
        [[nodiscard]] expected_void_t setup_readiness_timer_if_needed(
            std::optional<kmx::aio::readiness::descriptor::timer>& readiness_tick);

        /// @brief Suspends for one idle tick using the completion executor's native timeout.
        /// @return Success, or an error code if the timeout failed.
        task_returning_expected_void_t wait_completion_idle_tick()
        {
            auto timeout_res = co_await exec_.async_timeout(1'000'000ULL); // 1 ms
            if (!timeout_res)
                co_return std::unexpected(timeout_res.error());

            co_return expected_void_t {};
        }

        /// @brief Suspends for one idle tick by arming and awaiting the readiness watchdog timer.
        /// @param readiness_tick The watchdog timer created by @ref setup_readiness_timer_if_needed.
        /// @return Success, or an error code if the timer could not be armed or awaited.
        task_returning_expected_void_t wait_readiness_idle_tick(kmx::aio::readiness::descriptor::timer& readiness_tick);

        /// @brief Waits one idle tick on whichever timer this executor provides.
        /// @param readiness_tick The readiness timer, used only when the executor has no direct timeout.
        /// @return Nothing, or the reason the wait failed.
        task_returning_expected_void_t wait_idle_tick(std::optional<kmx::aio::readiness::descriptor::timer>& readiness_tick);

    public:
        /// @brief Shared event processing loop.
        task_returning_expected_void_t process();
    };

    template <typename Executor, typename UdpSocket>
    expected_void_t base_impl<Executor, UdpSocket>::setup_readiness_timer_if_needed(
        std::optional<kmx::aio::readiness::descriptor::timer>& readiness_tick)
    {
        if constexpr (requires(Executor& e) { e.async_timeout(std::uint64_t {}); })
            return {};
        else
        {
            auto timer_res = kmx::aio::readiness::descriptor::timer::create();
            if (!timer_res)
                return std::unexpected(timer_res.error());

            if (auto reg_res = exec_.register_fd(timer_res->get()); !reg_res)
                return std::unexpected(reg_res.error());

            readiness_tick.emplace(std::move(*timer_res));
            return {};
        }
    }

    template <typename Executor, typename UdpSocket>
    task_returning_expected_void_t base_impl<Executor, UdpSocket>::wait_readiness_idle_tick(
        kmx::aio::readiness::descriptor::timer& readiness_tick)
    {
        ::itimerspec one_ms {};
        one_ms.it_value.tv_nsec = readiness_idle_tick_ns_;

        if (auto arm_res = readiness_tick.set_time(0, one_ms); !arm_res)
            co_return std::unexpected(arm_res.error());

        auto tick_res = co_await readiness_tick.wait(exec_);
        if (!tick_res)
            co_return std::unexpected(tick_res.error());

        co_return expected_void_t {};
    }

    template <typename Executor, typename UdpSocket>
    task_returning_expected_void_t base_impl<Executor, UdpSocket>::wait_idle_tick(
        std::optional<kmx::aio::readiness::descriptor::timer>& readiness_tick)
    {
        // Which timer serves depends on the executor: a completion executor times out directly, while a
        // readiness one needs a descriptor to wait on.
        if constexpr (requires(Executor& e) { e.async_timeout(std::uint64_t {}); })
            co_return co_await wait_completion_idle_tick();
        else
            co_return co_await wait_readiness_idle_tick(*readiness_tick);
    }

    template <typename Executor, typename UdpSocket>
    task_returning_expected_void_t base_impl<Executor, UdpSocket>::process()
    {
        running_ = true;
        packet_buffer_t packet_buf {};
        ::msghdr msg {};
        ::iovec iov[1u] {};
        std::optional<kmx::aio::readiness::descriptor::timer> readiness_tick;
        [[maybe_unused]] timer_guard_t timer_guard {exec_, readiness_tick};

        if (auto setup_res = setup_readiness_timer_if_needed(readiness_tick); !setup_res)
            co_return std::unexpected(setup_res.error());

        bootstrap_initial_packets();

        while (running_)
        {
            drive_engine_once();

            ::sockaddr_storage peer_addr {};
            prepare_recv_message(packet_buf, peer_addr, msg, iov);

            auto recv_res = receive_once(packet_buf, msg, peer_addr);
            if (!recv_res)
                co_return std::unexpected(recv_res.error());

            if (*recv_res)
                if (auto idle_res = co_await wait_idle_tick(readiness_tick); !idle_res)
                    co_return std::unexpected(idle_res.error());
        }

        co_return expected_void_t {};
    }
}
