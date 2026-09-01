/// @file aio/benchmark/feature/backend_traits.hpp
/// @brief The two execution models behind one interface, so a scenario can be written once.
/// @details The executors deliberately share no base class - there is no virtual I/O anywhere in the
///          library, which is the point of it. A benchmark that wants to measure the same work on
///          both therefore has to bridge them somewhere, and doing it here, in a header only the
///          benchmark sees, keeps that bridge out of the library.
///
///          What the two models genuinely require differs, and this file preserves those differences
///          rather than papering over them. The readiness model needs its descriptors non-blocking
///          and registered with the executor before anything can wait on them; the completion model
///          needs neither. Forcing one model into the other's configuration would measure a set-up
///          nobody would ship, so each side is configured the way its own model asks to be, and only
///          the *work* is held identical.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cerrno>
    #include <chrono>
    #include <cstddef>
    #include <memory>
    #include <netinet/in.h>
    #include <optional>
    #include <string_view>
    #include <sys/socket.h>
    #include <system_error>
    #include <unistd.h>

    #include <kmx/aio/benchmark/harness.hpp>
    #include <kmx/aio/file_descriptor.hpp>
#endif

#if defined(KMX_AIO_FEATURE_READINESS)
    #include <kmx/aio/readiness/descriptor/timer.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/readiness/tcp/listener.hpp>
    #include <kmx/aio/readiness/tcp/stream.hpp>
    #include <kmx/aio/readiness/udp/endpoint.hpp>
#endif

#if defined(KMX_AIO_FEATURE_COMPLETION)
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/completion/tcp/listener.hpp>
    #include <kmx/aio/completion/tcp/stream.hpp>
    #include <kmx/aio/completion/timer.hpp>
    #include <kmx/aio/completion/udp/endpoint.hpp>
#endif

namespace kmx::aio::benchmark::feature
{
    /// @brief The address every scenario binds and connects to.
    /// @details Loopback throughout. These benchmarks compare two executors doing the same work, and
    ///          the wire is not part of that comparison - it would add a term neither executor
    ///          controls and that varies more between runs than the thing being measured. The
    ///          absolute figures are correspondingly optimistic against a real link; the ratio is
    ///          what the report is for.
    [[nodiscard]] inline ip_address_t loopback() noexcept
    {
        return make_ip_address(ipv4::localhost);
    }

    /// @brief Builds a loopback socket address for a port.
    /// @param port The port.
    /// @return The address, ready to hand to connect(2).
    [[nodiscard]] inline ::sockaddr_in loopback_address(const port_t port) noexcept
    {
        ::sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_port = ::htons(port);
        addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        return addr;
    }

    /// @brief Binds a socket to a loopback port.
    /// @details Through ::bind on the descriptor rather than through the socket wrapper, because only
    ///          one of the two models has a bind() on it: completion::udp::socket does,
    ///          readiness::udp::socket does not. Reaching for the descriptor is the only thing both
    ///          models can be asked to do here, and a benchmark is the wrong place to work around a
    ///          gap in the API - it would end up measuring the workaround.
    /// @param fd The socket to bind.
    /// @param port The loopback port, or zero to let the kernel choose.
    /// @return True when the bind succeeded.
    [[nodiscard]] inline bool bind_loopback(const fd_t fd, const port_t port) noexcept
    {
        const auto addr = loopback_address(port);
        return ::bind(fd, reinterpret_cast<const ::sockaddr*>(&addr), sizeof(addr)) == 0;
    }

    /// @brief Reads back the port the kernel actually assigned to a socket bound to port zero.
    /// @details Scenarios bind to port zero rather than picking a number, so two benchmark runs - or a
    ///          benchmark and whatever else is on the machine - cannot collide on a fixed port and
    ///          turn a measurement into a bind failure.
    /// @param fd The bound socket.
    /// @return The port, or zero when it could not be read.
    [[nodiscard]] inline port_t bound_port(const fd_t fd) noexcept
    {
        ::sockaddr_in addr {};
        ::socklen_t length = sizeof(addr);
        if (::getsockname(fd, reinterpret_cast<::sockaddr*>(&addr), &length) != 0)
            return 0u;

        return ::ntohs(addr.sin_port);
    }

#if defined(KMX_AIO_FEATURE_READINESS)

    /// @brief The epoll executor, as a scenario sees it.
    struct readiness_backend
    {
        using executor_t = readiness::executor;
        using tcp_listener_t = readiness::tcp::listener;
        using tcp_stream_t = readiness::tcp::stream;
        using udp_endpoint_t = readiness::udp::endpoint;

        /// @brief Which side of a comparison this is.
        static constexpr execution_model model = execution_model::readiness;

        /// @brief How this side is named in a case name.
        static constexpr std::string_view label = "epoll";

        /// @brief Extra socket(2) flags this model needs on every descriptor it drives.
        /// @details Non-blocking is not a tuning choice here: the readiness model works by reading
        ///          until EAGAIN and then waiting, so a blocking descriptor would park the event loop
        ///          inside the read and never reach the wait.
        static constexpr int socket_flags = SOCK_NONBLOCK;

        /// @brief Owns an executor for the duration of a scenario.
        /// @details The readiness executor derives from std::enable_shared_from_this, so it has to
        ///          live in a shared_ptr - the watchdog needs a share of it as well.
        class holder
        {
        public:
            /// @brief Creates the executor.
            /// @param config The configuration to build it from.
            /// @throws std::system_error if epoll creation fails.
            /// @throws std::bad_alloc if the executor cannot be allocated.
            explicit holder(const readiness::executor_config& config) noexcept(false): exec_(std::make_shared<executor_t>(config)) {}

            /// @brief Returns the executor.
            [[nodiscard]] executor_t& get() const noexcept { return *exec_; }

            /// @brief Returns a share of the executor, for anything that has to outlive the scenario body.
            [[nodiscard]] const std::shared_ptr<executor_t>& shared() const noexcept { return exec_; }

        private:
            /// @brief The executor.
            std::shared_ptr<executor_t> exec_;
        };

        /// @brief Builds an executor configured for a like-for-like comparison.
        /// @details resumption_mode::inline_on_io_thread, not the default. The completion executor
        ///          continues a coroutine on the thread that saw the completion, and this is the
        ///          readiness setting that does the same thing. Left at the default the readiness
        ///          side would additionally pay a scheduler hand-off per wake-up, and the row would
        ///          report that hand-off as though it were the cost of epoll. The default is measured
        ///          too, as its own separate case - it is what most callers get - but not here.
        /// @return The holder owning it.
        /// @throws std::system_error if epoll creation fails.
        /// @throws std::bad_alloc if the executor cannot be allocated.
        [[nodiscard]] static holder make() noexcept(false)
        {
            return holder {readiness::executor_config {
                .thread_count = 1u, .max_events = 64u, .timeout_ms = 50u, .resumption = readiness::resumption_mode::inline_on_io_thread}};
        }

        /// @brief Hands a descriptor to the executor.
        /// @param exec The executor.
        /// @param fd The descriptor.
        /// @return True when the executor took it.
        [[nodiscard]] static bool adopt(executor_t& exec, const fd_t fd) noexcept { return exec.register_fd(fd).has_value(); }

        /// @brief Reads exactly one buffer's worth, suspending whenever the descriptor is not ready.
        /// @param exec The executor to suspend on.
        /// @param fd The descriptor to read.
        /// @param buffer The destination, filled completely.
        /// @return True on success, false when the wait was cancelled or the peer went away.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] static task<bool> read_exact(executor_t& exec, const fd_t fd, const span_char_t buffer) noexcept(false)
        {
            std::size_t filled {};
            while (filled != buffer.size())
            {
                const auto n = ::read(fd, buffer.data() + filled, buffer.size() - filled);
                if (n > 0)
                {
                    filled += static_cast<std::size_t>(n);
                    continue;
                }

                if (n == 0)
                    co_return false;

                if (errno == EINTR)
                    continue;

                if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
                    co_return false;

                if (!co_await exec.wait_io(fd, readiness::event_type::read))
                    co_return false;
            }

            co_return true;
        }

        /// @brief Writes a whole buffer, suspending whenever the descriptor will not take more.
        /// @param exec The executor to suspend on.
        /// @param fd The descriptor to write.
        /// @param buffer The source, written completely.
        /// @return True on success, false when the wait was cancelled or the peer went away.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] static task<bool> write_exact(executor_t& exec, const fd_t fd, const cspan_char_t buffer) noexcept(false)
        {
            std::size_t sent {};
            while (sent != buffer.size())
            {
                const auto n = ::write(fd, buffer.data() + sent, buffer.size() - sent);
                if (n > 0)
                {
                    sent += static_cast<std::size_t>(n);
                    continue;
                }

                if (errno == EINTR)
                    continue;

                if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
                    co_return false;

                if (!co_await exec.wait_io(fd, readiness::event_type::write))
                    co_return false;
            }

            co_return true;
        }
        /// @brief Opens a connected client socket to a loopback port.
        /// @details The readiness model's connect is the three-step one the samples use: a
        ///          non-blocking connect(2) that reports EINPROGRESS, a wait for writability, and then
        ///          SO_ERROR to find out what actually happened. There is no shorter form of it in
        ///          this model, and pretending otherwise in the benchmark would understate what a
        ///          readiness connect costs.
        /// @param exec The executor to register the socket with and wait on.
        /// @param port The loopback port to connect to.
        /// @return The connected descriptor, or the error that stopped it.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] static task<file_descriptor::expected_t> connect(executor_t& exec, const port_t port) noexcept(false)
        {
            auto created = file_descriptor::create_socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
            if (!created)
                co_return std::unexpected(created.error());

            auto owner = std::move(*created);
            const auto fd = owner.get();

            const auto started = owner.connect(loopback(), port);
            const bool in_progress = !started && (started.error().value() == EINPROGRESS);
            if (!started && !in_progress)
                co_return std::unexpected(started.error());

            if (const auto registered = exec.register_fd(fd); !registered)
                co_return std::unexpected(registered.error());

            if (in_progress && !co_await exec.wait_io(fd, readiness::event_type::write))
            {
                exec.unregister_fd(fd);
                co_return std::unexpected(std::error_code {ECANCELED, std::system_category()});
            }

            int so_error {};
            ::socklen_t length = sizeof(so_error);
            if (const auto queried = owner.getsockopt(SOL_SOCKET, SO_ERROR, &so_error, &length); !queried)
            {
                exec.unregister_fd(fd);
                co_return std::unexpected(queried.error());
            }

            if (so_error != 0)
            {
                exec.unregister_fd(fd);
                co_return std::unexpected(std::error_code {so_error, std::system_category()});
            }

            co_return std::move(owner);
        }
        /// @brief A reusable one-shot timer, as a scenario sees it.
        /// @details The readiness model times things with a timerfd watched by epoll, so the handle
        ///          owns a descriptor and registers it once. Re-arming it per wait is what real code
        ///          does; creating a fresh timerfd for every wait would measure timerfd_create, which
        ///          the completion model has no equivalent of and would not be a comparison.
        class timer_handle
        {
        public:
            /// @brief Creates and registers a timer.
            /// @param exec The executor to register the timer descriptor with.
            /// @return The handle, or nothing when the timer could not be made.
            [[nodiscard]] static std::optional<timer_handle> create(executor_t& exec) noexcept
            {
                auto created = readiness::descriptor::timer::create();
                if (!created)
                    return std::nullopt;

                timer_handle handle {std::move(*created)};
                if (!exec.register_fd(handle.timer_.get()))
                    return std::nullopt;

                return handle;
            }

            /// @brief Arms the timer and suspends until it fires.
            /// @param exec The executor to suspend on.
            /// @param duration How long to wait.
            /// @return True when the timer fired, false when arming or waiting failed.
            /// @throws std::bad_alloc (coroutine frame allocation).
            [[nodiscard]] task<bool> wait_for(executor_t& exec, const std::chrono::nanoseconds duration) noexcept(false)
            {
                ::itimerspec spec {};
                spec.it_value.tv_sec = static_cast<std::time_t>(duration.count() / 1'000'000'000);
                spec.it_value.tv_nsec = static_cast<long>(duration.count() % 1'000'000'000);

                if (!timer_.set_time(0, spec))
                    co_return false;

                co_return (co_await timer_.wait(exec)).has_value();
            }

        private:
            /// @brief Wraps the created timer.
            explicit timer_handle(readiness::descriptor::timer&& timer) noexcept: timer_(std::move(timer)) {}

            /// @brief The timerfd.
            readiness::descriptor::timer timer_;
        };
    };

#endif // KMX_AIO_FEATURE_READINESS

#if defined(KMX_AIO_FEATURE_COMPLETION)

    /// @brief The io_uring executor, as a scenario sees it.
    struct completion_backend
    {
        using executor_t = completion::executor;
        using tcp_listener_t = completion::tcp::listener;
        using tcp_stream_t = completion::tcp::stream;
        using udp_endpoint_t = completion::udp::endpoint;

        /// @brief Which side of a comparison this is.
        static constexpr execution_model model = execution_model::completion;

        /// @brief How this side is named in a case name.
        static constexpr std::string_view label = "io_uring";

        /// @brief Extra socket(2) flags this model needs on every descriptor it drives.
        /// @details None. The kernel completes the operation rather than reporting readiness, so
        ///          there is nothing for a non-blocking flag to do here.
        static constexpr int socket_flags = 0;

        /// @brief Owns an executor for the duration of a scenario.
        class holder
        {
        public:
            /// @brief Creates the executor.
            /// @param config The configuration to build it from.
            /// @throws std::system_error if the ring cannot be set up.
            explicit holder(const completion::executor_config& config) noexcept(false): exec_(config) {}

            /// @brief Returns the executor.
            [[nodiscard]] executor_t& get() noexcept { return exec_; }

        private:
            /// @brief The executor.
            executor_t exec_;
        };

        /// @brief Builds an executor configured for a like-for-like comparison.
        /// @return The holder owning it.
        /// @throws std::system_error if the ring cannot be set up.
        [[nodiscard]] static holder make() noexcept(false) { return holder {completion::executor_config {.ring_entries = 256u}}; }

        /// @brief Hands a descriptor to the executor.
        /// @details Nothing to do - io_uring takes the descriptor with each operation. Present so a
        ///          scenario can call it unconditionally.
        /// @return True, always.
        [[nodiscard]] static bool adopt(executor_t&, const fd_t) noexcept { return true; }

        /// @brief Reads exactly one buffer's worth.
        /// @param exec The executor to submit to.
        /// @param fd The descriptor to read.
        /// @param buffer The destination, filled completely.
        /// @return True on success, false when the operation failed or the peer went away.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] static task<bool> read_exact(executor_t& exec, const fd_t fd, const span_char_t buffer) noexcept(false)
        {
            std::size_t filled {};
            while (filled != buffer.size())
            {
                const auto n = co_await exec.async_read(fd, span_char_t(buffer.data() + filled, buffer.size() - filled), 0u);
                if (!n || (*n == 0u))
                    co_return false;

                filled += *n;
            }

            co_return true;
        }

        /// @brief Writes a whole buffer.
        /// @param exec The executor to submit to.
        /// @param fd The descriptor to write.
        /// @param buffer The source, written completely.
        /// @return True on success, false when the operation failed or the peer went away.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] static task<bool> write_exact(executor_t& exec, const fd_t fd, const cspan_char_t buffer) noexcept(false)
        {
            std::size_t sent {};
            while (sent != buffer.size())
            {
                const auto n = co_await exec.async_write(fd, cspan_char_t(buffer.data() + sent, buffer.size() - sent), 0u);
                if (!n || (*n == 0u))
                    co_return false;

                sent += *n;
            }

            co_return true;
        }
        /// @brief Opens a connected client socket to a loopback port.
        /// @details One IORING_OP_CONNECT. The kernel reports the outcome in the completion, so there
        ///          is no EINPROGRESS to wait through and no SO_ERROR to read back afterwards.
        /// @param exec The executor to submit to.
        /// @param port The loopback port to connect to.
        /// @return The connected descriptor, or the error that stopped it.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] static task<file_descriptor::expected_t> connect(executor_t& exec, const port_t port) noexcept(false)
        {
            auto created = file_descriptor::create_socket(AF_INET, SOCK_STREAM, 0);
            if (!created)
                co_return std::unexpected(created.error());

            auto owner = std::move(*created);
            const auto addr = loopback_address(port);
            const auto connected = co_await exec.async_connect(owner.get(), reinterpret_cast<const ::sockaddr*>(&addr), sizeof(addr));
            if (!connected)
                co_return std::unexpected(connected.error());

            co_return std::move(owner);
        }
        /// @brief A reusable one-shot timer, as a scenario sees it.
        /// @details There is no descriptor here: the completion model submits IORING_OP_TIMEOUT to the
        ///          ring, so the handle holds nothing and every wait is one submission. That asymmetry
        ///          is the point of the timer comparison rather than something to hide.
        class timer_handle
        {
        public:
            /// @brief Creates a timer. Nothing can fail.
            /// @return The handle.
            [[nodiscard]] static std::optional<timer_handle> create(executor_t&) noexcept { return timer_handle {}; }

            /// @brief Submits a timeout and suspends until it fires.
            /// @param exec The executor to submit to.
            /// @param duration How long to wait.
            /// @return True when the timeout completed.
            /// @throws std::bad_alloc (coroutine frame allocation).
            [[nodiscard]] task<bool> wait_for(executor_t& exec, const std::chrono::nanoseconds duration) noexcept(false)
            {
                completion::timer timer {exec};
                co_return (co_await timer.wait(duration)).has_value();
            }
        };
    };

#endif // KMX_AIO_FEATURE_COMPLETION

    /// @brief True when both models are in this build, so a pairing has two sides to measure.
    static constexpr bool both_models_present =
#if defined(KMX_AIO_FEATURE_READINESS) && defined(KMX_AIO_FEATURE_COMPLETION)
        true;
#else
        false;
#endif

} // namespace kmx::aio::benchmark::feature
