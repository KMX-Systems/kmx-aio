/// @file api/kmx/aio/quic/endpoint.hpp
/// @brief A QUIC endpoint bound to one executor's spawn, timeout and poll operations.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_QUIC)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/quic/basic_endpoint.hpp>
        #include <kmx/aio/task.hpp>

        #include <cstdint>
        #include <utility>
    #endif

namespace kmx::aio::quic
{
    /// @brief A @ref basic_endpoint driven by @p Executor.
    /// @tparam Executor The model specific executor; only used for the socket I/O and the tick timer.
    ///
    /// @note The whole of the template: it binds the three operations the packet loop needs to one executor's
    ///       spelling of them. Everything else is compiled once, in basic_endpoint.
    template <typename Executor>
    class endpoint final: public basic_endpoint
    {
    public:
        /// @brief Constructs an endpoint bound to @p exec.
        explicit endpoint(Executor& exec) noexcept: exec_(&exec) {}

    private:
        void io_spawn(task<void>&& t) noexcept(false) override { exec_->spawn(std::move(t)); }

        [[nodiscard]] task_returning_expected_void_t io_timeout(const std::uint64_t duration_ns) noexcept(false) override
        {
            return exec_->async_timeout(duration_ns);
        }

        [[nodiscard]] task<expected_int_t> io_poll(const fd_t fd, const unsigned poll_mask) noexcept(false) override
        {
            return exec_->async_poll(fd, poll_mask);
        }

        Executor* exec_ {}; ///< Drives the socket I/O.
    };
}

#endif // KMX_AIO_FEATURE_QUIC
