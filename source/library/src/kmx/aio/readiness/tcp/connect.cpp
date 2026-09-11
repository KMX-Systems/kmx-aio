/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/readiness/tcp/connect.hpp>

#include <kmx/aio/error_code.hpp>

#include <cerrno>
#include <chrono>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <optional>
#include <poll.h>
#include <system_error>
#include <utility>

namespace kmx::aio::readiness::tcp
{
    /// @brief How long one wait for writability lasts before the socket is looked at again.
    /// @details Descriptors are registered edge-triggered, and a loopback connect can complete - raising the single
    ///          write edge it will ever raise - before the wait has subscribed to it. A wait nobody wakes would hang the
    ///          connect for good, so each wait is bounded, and the socket is asked directly whether the connect is done
    ///          whenever a wait ends, woken or not.
    static constexpr std::uint32_t recheck_interval_ms = 20u;

    /// @brief The monotonic millisecond stamp the executor's deadlines are expressed in.
    [[nodiscard]] static std::uint32_t now_ms() noexcept
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    /// @brief Creates a non-blocking stream socket for @p family, with Nagle disabled, and registers it.
    [[nodiscard]] static file_descriptor::expected_t prepare_socket(executor& exec, const int family) noexcept
    {
        auto socket = file_descriptor::create_socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (!socket)
            return std::unexpected(socket.error());

        // Every caller exchanges small request and response frames, which Nagle would hold back.
        const int one = 1;
        static_cast<void>(::setsockopt(socket->get(), IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)));
        if (const auto registered = exec.register_fd(socket->get()); !registered)
            return std::unexpected(registered.error());
        return socket;
    }

    /// @brief Indicates whether a connect in progress has finished, successfully or not.
    [[nodiscard]] static bool connect_finished(const fd_t fd) noexcept
    {
        ::pollfd descriptor {fd, POLLOUT, 0};
        return (::poll(&descriptor, 1u, 0) > 0) && ((descriptor.revents & (POLLOUT | POLLERR | POLLHUP)) != 0);
    }

    /// @brief Reads the outcome of a finished connect from `SO_ERROR`.
    [[nodiscard]] static expected_void_t connect_outcome(const fd_t fd) noexcept
    {
        int so_error {};
        ::socklen_t length {sizeof(so_error)};
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &length) != 0)
            return std::unexpected(std::error_code(errno, std::generic_category()));
        if (so_error != 0)
            return std::unexpected(std::error_code(so_error, std::generic_category()));
        return {};
    }

    /// @brief Connects a prepared socket, waiting in bounded slices until it has, or until @p deadline_ms when set.
    [[nodiscard]] static task_returning_expected_void_t complete_connect(executor& exec, const fd_t fd, const sockaddr* const address,
                                                                         const ::socklen_t address_length,
                                                                         const std::optional<std::uint32_t> deadline_ms) noexcept(false)
    {
        if (::connect(fd, address, address_length) == 0)
            co_return expected_void_t {};
        if (errno != EINPROGRESS)
            co_return std::unexpected(std::error_code(errno, std::generic_category()));

        while (!connect_finished(fd))
        {
            const auto slice = now_ms() + recheck_interval_ms;
            if (deadline_ms.has_value() && (static_cast<std::int32_t>(*deadline_ms - now_ms()) <= 0))
                co_return std::unexpected(std::make_error_code(std::errc::timed_out));
            const auto until = (deadline_ms.has_value() && (static_cast<std::int32_t>(*deadline_ms - slice) < 0)) ? *deadline_ms : slice;
            if (co_await exec.wait_io_until(fd, event_type::write, until) == executor::wait_status::cancelled)
                co_return std::unexpected(to_std_error_code(error_code::operation_cancelled));
        }
        co_return connect_outcome(fd);
    }

    /// @brief The shared body of @ref connect and @ref connect_until.
    [[nodiscard]] static task<file_descriptor::expected_t> open_connection(executor& exec, const sockaddr* const address,
                                                                           const ::socklen_t address_length,
                                                                           const std::optional<std::uint32_t> deadline_ms) noexcept(false)
    {
        if ((address == nullptr) || (address_length <= 0))
            co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        auto socket = prepare_socket(exec, address->sa_family);
        if (!socket)
            co_return std::unexpected(socket.error());

        if (const auto connected = co_await complete_connect(exec, socket->get(), address, address_length, deadline_ms); !connected)
        {
            exec.unregister_fd(socket->get());
            co_return std::unexpected(connected.error());
        }
        co_return std::move(*socket);
    }

    task<file_descriptor::expected_t> connect(executor& exec, const sockaddr* const address, const ::socklen_t address_length) noexcept(false)
    {
        co_return co_await open_connection(exec, address, address_length, std::nullopt);
    }

    task<file_descriptor::expected_t> connect_until(executor& exec, const sockaddr* const address, const ::socklen_t address_length,
                                                    const std::uint32_t deadline_ms) noexcept(false)
    {
        co_return co_await open_connection(exec, address, address_length, deadline_ms);
    }
}
