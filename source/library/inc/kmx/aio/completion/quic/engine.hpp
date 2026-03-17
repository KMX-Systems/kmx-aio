/// @file aio/completion/quic/engine.hpp
/// @brief Completion-model QUIC engine using lsquic over io_uring-based UDP transport.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <expected>
    #include <functional>
    #include <memory>
    #include <span>
    #include <system_error>

    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::completion::quic
{
    /// @brief QUIC engine for the completion (io_uring) model.
    /// @details Identical interface to readiness::quic::engine, but the underlying
    ///          UDP datagram transport uses io_uring completion I/O.
    /// @note This is a forward declaration / interface specification.
    ///       The full implementation requires linking against lsquic.
    class engine
    {
    public:
        /// @brief Callback invoked when a new QUIC stream is accepted.
        using stream_handler_t = std::function<task<void>(std::span<char>)>;

        /// @brief Default constructor creates an uninitialized engine.
        engine() noexcept = default;

        /// @brief Non-copyable.
        engine(const engine&) = delete;
        /// @brief Non-copyable.
        engine& operator=(const engine&) = delete;

        /// @brief Move constructor.
        engine(engine&&) noexcept = default;
        /// @brief Move assignment is disabled.
        engine& operator=(engine&&) noexcept = delete;

        /// @brief Destructor.
        ~engine() noexcept = default;

        /// @brief Starts the QUIC engine, binding to the specified address.
        /// @param ip   IP address to bind to.
        /// @param port Port number to bind to.
        /// @return Success or an error code.
        [[nodiscard]] task<std::expected<void, std::error_code>>
            start(ip_address_t ip, port_t port) noexcept(false);

        /// @brief Processes pending QUIC events (called from the event loop).
        /// @return Success or an error code.
        [[nodiscard]] task<std::expected<void, std::error_code>> process() noexcept(false);
    };

} // namespace kmx::aio::completion::quic
