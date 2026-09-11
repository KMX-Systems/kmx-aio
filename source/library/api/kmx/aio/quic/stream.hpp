/// @file api/kmx/aio/quic/stream.hpp
/// @brief A QUIC stream, presented as an ordered reliable byte stream.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_QUIC)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/promise_base.hpp>
        #include <kmx/aio/quic/transport.hpp>
        #include <kmx/aio/task.hpp>

        #include <cstdint>
        #include <memory>
        #include <utility>
    #endif

namespace kmx::aio::quic
{
    /// @brief A QUIC stream, presented as an ordered reliable byte stream.
    class stream
    {
    public:
        /// @brief Wraps @p state, which the endpoint owns.
        explicit stream(std::shared_ptr<stream_state> state) noexcept: state_(std::move(state)) {}

        stream(const stream&) = delete;
        stream& operator=(const stream&) = delete;
        stream(stream&&) noexcept = default;
        stream& operator=(stream&&) = delete;
        ~stream() noexcept = default;

        /// @brief The stream identifier, or zero once closed.
        [[nodiscard]] std::uint64_t id() const noexcept;

        /// @brief Whether the stream is still usable.
        [[nodiscard]] bool is_open() const noexcept { return state_ && !state_->closed; }

        /// @brief Reads whatever has arrived.
        /// @param out Destination.
        /// @return Bytes read; zero once the peer has finished and nothing is left.
        [[nodiscard]] task_returning_expected_size_t read(span_char_t out) noexcept(false);

        /// @brief Writes every byte, suspending until lsquic has accepted them all.
        [[nodiscard]] task_returning_expected_void_t write_all(cspan_char_t in) noexcept(false);

        /// @brief Ends this side of the stream.
        void shutdown_write() noexcept;

    private:
        /// @brief Shared coroutine mechanics for stream waiters.
        template <coroutine_handle_t stream_state::* waiter>
        struct awaiter_base
        {
            stream_state& state;

            void await_suspend(const coroutine_handle_t handle) const noexcept { state.*waiter = handle; }
            void await_resume() const noexcept {}
        };

        /// @brief Suspends until bytes are available, the peer finishes, or the stream fails.
        struct readable: awaiter_base<&stream_state::reader>
        {
            [[nodiscard]] bool await_ready() const noexcept
            {
                return !state.incoming.empty() || state.fin_received || state.closed || static_cast<bool>(state.error);
            }
        };

        /// @brief Suspends until everything queued has been handed to lsquic.
        struct flushed: awaiter_base<&stream_state::writer>
        {
            [[nodiscard]] bool await_ready() const noexcept
            {
                return state.outgoing.empty() || state.closed || static_cast<bool>(state.error);
            }
        };

        std::shared_ptr<stream_state> state_ {}; ///< Shared with the endpoint's callbacks.
    };
}

#endif // KMX_AIO_FEATURE_QUIC
