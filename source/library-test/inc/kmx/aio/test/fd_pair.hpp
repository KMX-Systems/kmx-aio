/// @file aio/test/fd_pair.hpp
/// @brief Descriptor-pair fixtures and ephemeral-port binding, shared by the tests.
/// @details A test that needs a descriptor to wait on almost always wants a pair: one end it hands to
///          the code under test, one end it pokes from the test thread to make an event happen. Both
///          ends have to be closed however the test leaves, including through a failed REQUIRE, which
///          is what makes these RAII types rather than free functions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cerrno>
    #include <expected>
    #include <system_error>
    #include <utility>

    #include <fcntl.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>

    #include <kmx/aio/basic_types.hpp>
#endif

namespace kmx::aio::test
{
    /// @brief A connected pair of non-blocking sockets, closed on destruction unless released.
    /// @details AF_UNIX rather than AF_INET on purpose: the pair is connected the moment it exists, so
    ///          no test has to wait for a handshake it is not trying to exercise.
    class socket_pair
    {
    public:
        /// @brief Creates the pair; check @ref valid before use.
        socket_pair() noexcept { valid_ = ::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds_) == 0; }

        socket_pair(const socket_pair&) = delete;
        socket_pair& operator=(const socket_pair&) = delete;

        /// @brief Closes whichever ends are still owned.
        ~socket_pair() noexcept
        {
            for (int& fd: fds_)
                if (fd >= 0)
                    ::close(fd);
        }

        /// @brief Whether ::socketpair() succeeded.
        /// @return True when both descriptors are open.
        [[nodiscard]] bool valid() const noexcept { return valid_; }

        /// @brief The end handed to the code under test.
        /// @return The descriptor, or -1 once released.
        [[nodiscard]] int local() const noexcept { return fds_[0]; }

        /// @brief The end the test drives to produce events.
        /// @return The descriptor.
        [[nodiscard]] int peer() const noexcept { return fds_[1]; }

        /// @brief Gives up ownership of the local end, for handing to a stream that will close it.
        /// @return The descriptor, now the caller's to close.
        [[nodiscard]] int release_local() noexcept { return std::exchange(fds_[0], -1); }

    private:
        int fds_[2] {-1, -1};
        bool valid_ = false;
    };

    /// @brief A pipe whose ends are closed on destruction, individually closable before then.
    /// @details Closing one end on purpose is how a test produces EOF or EPIPE, so both ends can be
    ///          closed early and the destructor tolerates that.
    class pipe_pair
    {
    public:
        /// @brief Creates the pipe; check @ref valid before use.
        /// @param flags Passed to ::pipe2; O_CLOEXEC by default, O_NONBLOCK where a test needs it.
        explicit pipe_pair(const int flags = O_CLOEXEC) noexcept { valid_ = ::pipe2(fds_, flags) == 0; }

        pipe_pair(const pipe_pair&) = delete;
        pipe_pair& operator=(const pipe_pair&) = delete;

        /// @brief Closes whichever ends are still open.
        ~pipe_pair() noexcept
        {
            close_read();
            close_write();
        }

        /// @brief Whether ::pipe2() succeeded.
        /// @return True when both descriptors are open.
        [[nodiscard]] bool valid() const noexcept { return valid_; }

        /// @brief The readable end.
        /// @return The descriptor, or -1 once closed.
        [[nodiscard]] int read_end() const noexcept { return fds_[0]; }

        /// @brief The writable end.
        /// @return The descriptor, or -1 once closed.
        [[nodiscard]] int write_end() const noexcept { return fds_[1]; }

        /// @brief Closes the write end, which is what makes the read end report EOF.
        void close_write() noexcept
        {
            if (fds_[1] >= 0)
            {
                ::close(fds_[1]);
                fds_[1] = -1;
            }
        }

        /// @brief Closes the read end, which is what makes a write report EPIPE.
        void close_read() noexcept
        {
            if (fds_[0] >= 0)
            {
                ::close(fds_[0]);
                fds_[0] = -1;
            }
        }

    private:
        int fds_[2] {-1, -1};
        bool valid_ = false;
    };

    /// @brief Binds @p fd to a loopback port the kernel picks, and reports which one.
    /// @details Hard-coding a port makes a test fail when the machine happens to be using it, and makes
    ///          two tests in the same binary collide. Binding port 0 and asking afterwards does not.
    /// @param fd An unbound AF_INET socket.
    /// @return The bound port in host order, or the errno that bind() or getsockname() reported.
    [[nodiscard]] inline std::expected<port_t, std::error_code> bind_ephemeral_port(const int fd) noexcept
    {
        ::sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        address.sin_port = 0u;

        if (::bind(fd, reinterpret_cast<const ::sockaddr*>(&address), sizeof(address)) != 0)
            return std::unexpected(std::error_code(errno, std::system_category()));

        ::sockaddr_in bound {};
        auto length = static_cast<::socklen_t>(sizeof(bound));
        if (::getsockname(fd, reinterpret_cast<::sockaddr*>(&bound), &length) != 0)
            return std::unexpected(std::error_code(errno, std::system_category()));

        return ::ntohs(bound.sin_port);
    }

} // namespace kmx::aio::test
