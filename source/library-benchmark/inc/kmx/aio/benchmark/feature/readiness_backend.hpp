/// @file inc/kmx/aio/benchmark/feature/readiness_backend.hpp
/// @brief The epoll executor behind the interface a scenario is written against.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_READINESS)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/benchmark/feature/backend_traits.hpp>
        #include <kmx/aio/benchmark/harness.hpp>
        #include <kmx/aio/file_descriptor.hpp>
        #include <kmx/aio/readiness/basic_types.hpp>
        #include <kmx/aio/readiness/descriptor/timer.hpp>
        #include <kmx/aio/readiness/executor.hpp>
        #include <kmx/aio/readiness/tcp/listener.hpp>
        #include <kmx/aio/readiness/tcp/stream.hpp>
        #include <kmx/aio/readiness/udp/endpoint.hpp>
        #include <kmx/aio/task.hpp>

        #include <cerrno>
        #include <chrono>
        #include <cstddef>
        #include <ctime>
        #include <expected>
        #include <memory>
        #include <optional>
        #include <string_view>
        #include <system_error>
        #include <utility>
        #include <sys/socket.h>
        #include <sys/timerfd.h>
        #include <unistd.h>
    #endif

namespace kmx::aio::benchmark::feature
{
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
}

#endif // KMX_AIO_FEATURE_READINESS
