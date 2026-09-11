/// @file inc/kmx/aio/benchmark/feature/completion_backend.hpp
/// @brief The io_uring executor behind the interface a scenario is written against.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_COMPLETION)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/benchmark/feature/backend_traits.hpp>
        #include <kmx/aio/benchmark/harness.hpp>
        #include <kmx/aio/completion/executor.hpp>
        #include <kmx/aio/completion/tcp/listener.hpp>
        #include <kmx/aio/completion/tcp/stream.hpp>
        #include <kmx/aio/completion/timer.hpp>
        #include <kmx/aio/completion/udp/endpoint.hpp>
        #include <kmx/aio/file_descriptor.hpp>
        #include <kmx/aio/task.hpp>

        #include <chrono>
        #include <cstddef>
        #include <expected>
        #include <optional>
        #include <string_view>
        #include <utility>
        #include <sys/socket.h>
    #endif

namespace kmx::aio::benchmark::feature
{
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
        static constexpr int socket_flags {};

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
}

#endif // KMX_AIO_FEATURE_COMPLETION
