/// @file aio/readiness/tcp/stream.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/readiness/tcp/stream.hpp>

#include <kmx/aio/error_code.hpp>

#include <kmx/aio/readiness/openonload/extensions.hpp>

#include <sys/socket.h>
#include <system_error>

namespace kmx::aio::readiness::tcp
{
    task_returning_expected_size_t stream::read(const std::span<char> buffer) noexcept(false)
    {
        const bool is_onload_accel = (exec_.get_active_backend() == active_backend::openonload) && openonload::is_accelerated_fd(fd_.get());

        for (std::size_t total = 0u;;)
        {
            const std::size_t remaining = buffer.size() - total;

            if (is_onload_accel)
            {
                const std::span<char> remaining_span {buffer.data() + total, remaining};
                auto zc_res = openonload::zero_copy_receive(fd_.get(), remaining_span);

                if (zc_res)
                {
                    const std::size_t n = *zc_res;
                    if (n > 0)
                    {
                        total += n;
                        if (total == buffer.size())
                            co_return total;
                        continue;
                    }
                    else
                        co_return total; // EOF
                }
                else
                {
                    const auto err = zc_res.error();
                    if (err.category() == std::system_category() && would_block(err.value()))
                    {
                        if (total != 0u)
                            co_return total;

                        if (!co_await exec_.wait_io(fd_.get(), event_type::read))
                            co_return std::unexpected(to_std_error_code(error_code::operation_cancelled));
                        continue;
                    }
                    if (err != std::errc::function_not_supported)
                        co_return std::unexpected(err);
                    // Fallback to `::read` if unsupported
                }
            }

            const ssize_t n = ::read(fd_.get(), buffer.data() + total, remaining);
            if (n > 0)
            {
                total += static_cast<std::size_t>(n);
                if (total == buffer.size())
                    co_return total;
                continue;
            }

            if (n == 0)
                co_return total;

            if (would_block(errno))
            {
                if (total != 0u)
                    co_return total;

                if (!co_await exec_.wait_io(fd_.get(), event_type::read))
                    co_return std::unexpected(to_std_error_code(error_code::operation_cancelled));
                continue;
            }

            co_return std::unexpected(error_from_errno());
        }
    }

    task_returning_expected_size_t stream::write(const cspan_char_t buffer) noexcept(false)
    {
        const bool is_onload_accel = (exec_.get_active_backend() == active_backend::openonload) && openonload::is_accelerated_fd(fd_.get());

        while (true)
        {
            if (is_onload_accel)
            {
                auto zc_res = openonload::zero_copy_send(fd_.get(), buffer);
                if (zc_res)
                {
                    const std::size_t written = *zc_res;
                    // If Onload wrote anything, return immediately since it bypassed the kernel.
                    if (written > 0)
                        co_return written;
                }
                else
                {
                    // Fall back if Onload hardware queues are busy or strictly unsupported.
                    // If `zero_copy_send` yields `EAGAIN`, it's often better to just try standard `send`
                    // which is transparently intercepted by Onload and may buffer internally in userspace.
                    const auto err = zc_res.error();
                    if ((err.category() == std::system_category()) && would_block(err.value()))
                    {
                        if (!co_await exec_.wait_io(fd_.get(), event_type::write))
                            co_return std::unexpected(to_std_error_code(error_code::operation_cancelled));
                        continue;
                    }

                    if ((err != std::errc::resource_unavailable_try_again) && (err != std::errc::function_not_supported))
                        // Any other fatal error from hardware
                        co_return std::unexpected(err);
                    // Proceed to fallback `::send`
                }
            }

            const ssize_t n = ::send(fd_.get(), buffer.data(), buffer.size(), MSG_NOSIGNAL);
            if (n > 0)
                co_return static_cast<std::size_t>(n);

            if (n == 0)
                // Treat zero-byte write as a closed connection to prevent tight loops.
                co_return std::unexpected(std::make_error_code(std::errc::broken_pipe));

            if (would_block(errno))
            {
                if (!co_await exec_.wait_io(fd_.get(), event_type::write))
                    co_return std::unexpected(to_std_error_code(error_code::operation_cancelled));
                continue;
            }

            co_return std::unexpected(error_from_errno());
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
