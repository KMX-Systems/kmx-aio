/// @file aio/completion/tcp/listener.hpp
/// @brief Completion-model TCP listener using io_uring IORING_OP_ACCEPT.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <expected>
    #include <memory>
    #include <system_error>

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/descriptor/file.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::completion::tcp
{
    // Forward declaration
    class stream;

    /// @brief Asynchronous TCP listener backed by io_uring accept.
    /// @details Uses IORING_OP_ACCEPT to accept connections without polling
    ///          for readiness first, reducing the accept-to-first-byte latency.
    class listener
    {
    public:
        /// @brief Result type for non-coroutine operations.
        using result_t = std::expected<void, std::error_code>;

        /// @brief Creates a listener bound to the specified IP and port.
        /// @param exec The completion executor.
        /// @param ip   IP address to bind to.
        /// @param port Port number to bind to.
        /// @throws std::system_error if socket creation, bind, or option setting fails.
        listener(std::shared_ptr<executor> exec, ip_address_t ip, port_t port) noexcept(false);

        /// @brief Destructor.
        ~listener() noexcept = default;

        /// @brief Non-copyable.
        listener(const listener&) = delete;
        /// @brief Non-copyable.
        listener& operator=(const listener&) = delete;

        /// @brief Move constructor.
        listener(listener&&) noexcept = default;
        /// @brief Move assignment is disabled.
        listener& operator=(listener&&) noexcept = delete;

        /// @brief Starts listening for connections.
        /// @param backlog Maximum pending connection queue length.
        /// @return Success or an error code.
        [[nodiscard]] result_t listen(int backlog = 128) noexcept;

        /// @brief Asynchronously accepts a new connection via io_uring.
        /// @return A task yielding the accepted client file descriptor, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] task<std::expected<descriptor::file, std::error_code>> accept() noexcept(false);

        /// @brief Returns the listening socket file descriptor.
        [[nodiscard]] fd_t get_fd() const noexcept { return fd_.get(); }

    private:
        std::shared_ptr<executor> exec_;
        descriptor::file fd_;
    };

} // namespace kmx::aio::completion::tcp
