/// @file inc/kmx/aio/test/socket_pair.hpp
/// @brief A connected socket-pair fixture, shared by the tests.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details A test that needs a descriptor to wait on almost always wants a pair: one end it hands to
///          the code under test, one end it pokes from the test thread to make an event happen. Both
///          ends have to be closed however the test leaves, including through a failed REQUIRE, which
///          is what makes these RAII types rather than free functions.
#pragma once
#ifndef PCH
    #include <utility>
    #include <sys/socket.h>
    #include <unistd.h>
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
        bool valid_ {};
    };
}
