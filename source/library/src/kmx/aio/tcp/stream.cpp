#include "kmx/aio/tcp/stream.hpp"

#include "kmx/logger.hpp"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <system_error>
#include <sys/socket.h>

namespace kmx::aio::tcp
{
    stream::result_task stream::read(const std::span<char> buffer) noexcept(false)
    {
        for (std::size_t total = 0u; ; )
        {
            const std::size_t remaining = buffer.size() - total;
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

                co_await exec_.wait_io(fd_.get(), event_type::read);
                continue;
            }

            co_return std::unexpected(error_from_errno());
        }
    }

    stream::result_task stream::write(const std::span<const char> buffer) noexcept(false)
    {
        while (true)
        {
            const ssize_t n = ::send(fd_.get(), buffer.data(), buffer.size(), MSG_NOSIGNAL);
            if (n > 0)
                co_return static_cast<std::size_t>(n);

            if (n == 0)
            {
                // Treat zero-byte write as a closed connection to prevent tight loops.
                co_return std::unexpected(std::make_error_code(std::errc::broken_pipe));
            }

            if (would_block(errno))
            {
                co_await exec_.wait_io(fd_.get(), event_type::write);
                continue;
            }

            co_return std::unexpected(error_from_errno());
        }
    }

    task<std::expected<void, std::error_code>> stream::write_all(const std::span<const char> buffer) noexcept(false)
    {
        for (std::size_t offset {}; offset < buffer.size();)
        {
            const std::span<const char> chunk {buffer.data() + offset, buffer.size() - offset};
            const auto res = co_await write(chunk);
            if (!res)
                co_return std::unexpected(res.error());

            offset += *res;
        }

        co_return std::expected<void, std::error_code> {};
    }

} // namespace kmx::aio::tcp
