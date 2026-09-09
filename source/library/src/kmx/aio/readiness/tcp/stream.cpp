/// @file aio/readiness/tcp/stream.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/readiness/tcp/stream.hpp>

#include <kmx/aio/error_code.hpp>

#include <kmx/aio/readiness/openonload/extensions.hpp>

#include <sys/socket.h>
#include <system_error>

namespace kmx::aio::readiness::tcp
{
    /// @brief What one attempt at moving bytes produced.
    /// @details The accelerated path and the ordinary syscall answer in the same terms, so the loops
    ///          below decide what to do next without knowing which of the two actually ran.
    enum class transfer_outcome
    {
        progressed,  ///< Bytes moved and more may follow.
        finished,    ///< The transfer is done, or the peer closed.
        blocked,     ///< Nothing available; wait for readiness and try again.
        unsupported, ///< This path cannot serve the request; the ordinary syscall still can.
        failed,      ///< Give up and report the error.
    };

    /// @brief Tries a zero-copy receive on an accelerated socket.
    /// @param fd The socket.
    /// @param into Where to receive.
    /// @param moved Receives how many bytes arrived.
    /// @param error Receives the error, when there is one.
    /// @return What the attempt produced.
    [[nodiscard]] static transfer_outcome onload_receive(const int fd, const std::span<char> into, std::size_t& moved,
                                                         std::error_code& error) noexcept
    {
        auto result = openonload::zero_copy_receive(fd, into);
        if (result)
        {
            moved = *result;
            return (moved > 0u) ? transfer_outcome::progressed : transfer_outcome::finished;
        }

        error = result.error();
        if ((error.category() == std::system_category()) && would_block(error.value()))
            return transfer_outcome::blocked;
        // Onload declining the operation is not a failure: the ordinary syscall below still serves it,
        // and Onload intercepts that transparently anyway.
        return (error == std::errc::function_not_supported) ? transfer_outcome::unsupported : transfer_outcome::failed;
    }

    /// @copydoc onload_receive
    /// @brief Tries a zero-copy send on an accelerated socket.
    [[nodiscard]] static transfer_outcome onload_send(const int fd, const cspan_char_t buffer, std::size_t& moved,
                                                      std::error_code& error) noexcept
    {
        auto result = openonload::zero_copy_send(fd, buffer);
        if (result)
        {
            if (*result == 0u)
                return transfer_outcome::unsupported;

            // Onload bypassed the kernel; whatever it accepted is what this write moved.
            moved = *result;
            return transfer_outcome::finished;
        }

        error = result.error();
        if ((error.category() == std::system_category()) && would_block(error.value()))
            return transfer_outcome::blocked;
        // A busy hardware queue, or one that cannot serve this at all, falls through to ::send - which
        // Onload also intercepts and may buffer in user space. Anything else is a real failure.
        return ((error == std::errc::resource_unavailable_try_again) || (error == std::errc::function_not_supported))
                   ? transfer_outcome::unsupported
                   : transfer_outcome::failed;
    }

    /// @brief Reads one chunk, preferring the accelerated path when the socket has one.
    /// @param fd The socket.
    /// @param accelerated Whether the accelerated path is available for this socket.
    /// @param into Where to read.
    /// @param moved Receives how many bytes arrived.
    /// @param error Receives the error, when there is one.
    /// @return What the attempt produced; never @ref transfer_outcome::unsupported.
    [[nodiscard]] static transfer_outcome read_chunk(const int fd, const bool accelerated, const std::span<char> into,
                                                     std::size_t& moved, std::error_code& error) noexcept
    {
        if (accelerated)
        {
            const auto outcome = onload_receive(fd, into, moved, error);
            if (outcome != transfer_outcome::unsupported)
                return outcome;
        }

        const ssize_t n = ::read(fd, into.data(), into.size());
        if (n > 0)
        {
            moved = static_cast<std::size_t>(n);
            return transfer_outcome::progressed;
        }
        if (n == 0)
            return transfer_outcome::finished;
        if (would_block(errno))
            return transfer_outcome::blocked;

        error = error_from_errno();
        return transfer_outcome::failed;
    }

    /// @copydoc read_chunk
    /// @brief Writes one chunk, preferring the accelerated path when the socket has one.
    [[nodiscard]] static transfer_outcome write_chunk(const int fd, const bool accelerated, const cspan_char_t buffer,
                                                      std::size_t& moved, std::error_code& error) noexcept
    {
        if (accelerated)
        {
            const auto outcome = onload_send(fd, buffer, moved, error);
            if (outcome != transfer_outcome::unsupported)
                return outcome;
        }

        const ssize_t n = ::send(fd, buffer.data(), buffer.size(), MSG_NOSIGNAL);
        if (n > 0)
        {
            moved = static_cast<std::size_t>(n);
            return transfer_outcome::finished;
        }
        if (n == 0)
        {
            // A zero-byte write means the connection is gone; saying so stops a tight loop.
            error = std::make_error_code(std::errc::broken_pipe);
            return transfer_outcome::failed;
        }
        if (would_block(errno))
            return transfer_outcome::blocked;

        error = error_from_errno();
        return transfer_outcome::failed;
    }

    /// @brief Reports whether this socket can use the accelerated path.
    [[nodiscard]] static bool accelerated_socket(const executor& exec, const int fd) noexcept
    {
        return (exec.get_active_backend() == active_backend::openonload) && openonload::is_accelerated_fd(fd);
    }

    task_returning_expected_size_t stream::read(const std::span<char> buffer) noexcept(false)
    {
        const bool accelerated = accelerated_socket(exec_, fd_.get());

        for (std::size_t total = 0u;;)
        {
            std::size_t moved {};
            std::error_code error {};
            switch (read_chunk(fd_.get(), accelerated, {buffer.data() + total, buffer.size() - total}, moved, error))
            {
                case transfer_outcome::progressed:
                    total += moved;
                    if (total == buffer.size())
                        co_return total;
                    break;
                case transfer_outcome::finished:
                    co_return total;
                case transfer_outcome::blocked:
                    // Bytes already in hand are returned rather than waited past: a short read is what
                    // reading a stream means.
                    if (total != 0u)
                        co_return total;
                    if (!co_await exec_.wait_io(fd_.get(), event_type::read))
                        co_return std::unexpected(to_std_error_code(error_code::operation_cancelled));
                    break;
                default:
                    co_return std::unexpected(error);
            }
        }
    }

    task_returning_expected_size_t stream::write(const cspan_char_t buffer) noexcept(false)
    {
        const bool accelerated = accelerated_socket(exec_, fd_.get());

        for (;;)
        {
            std::size_t moved {};
            std::error_code error {};
            switch (write_chunk(fd_.get(), accelerated, buffer, moved, error))
            {
                case transfer_outcome::finished:
                    co_return moved;
                case transfer_outcome::blocked:
                    if (!co_await exec_.wait_io(fd_.get(), event_type::write))
                        co_return std::unexpected(to_std_error_code(error_code::operation_cancelled));
                    break;
                default:
                    co_return std::unexpected(error);
            }
        }
    }

    task_returning_expected_void_t stream::write_all(const cspan_char_t buffer) noexcept(false)
    {
        for (std::size_t offset {}; offset < buffer.size();)
        {
            const cspan_char_t chunk {buffer.data() + offset, buffer.size() - offset};
            const auto res = co_await write(chunk);
            if (!res)
                co_return std::unexpected(res.error());

            offset += *res;
        }

        co_return expected_void_t {};
    }

} // namespace kmx::aio::readiness::tcp
