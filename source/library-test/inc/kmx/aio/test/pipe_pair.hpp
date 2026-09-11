/// @file inc/kmx/aio/test/pipe_pair.hpp
/// @brief A pipe fixture whose ends can be closed early, shared by the tests.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details A test that needs a descriptor to wait on almost always wants a pair: one end it hands to
///          the code under test, one end it pokes from the test thread to make an event happen. Both
///          ends have to be closed however the test leaves, including through a failed REQUIRE, which
///          is what makes these RAII types rather than free functions.
#pragma once
#ifndef PCH
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace kmx::aio::test
{
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
        bool valid_ {};
    };
}
