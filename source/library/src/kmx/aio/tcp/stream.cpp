#include "kmx/aio/tcp/stream.hpp"

#include "kmx/logger.hpp"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace kmx::aio::tcp
{
    stream::result_task stream::read(const std::span<char> buffer) noexcept(false)
    {
        while (true)
        {
            const ssize_t n = ::read(fd_.get(), buffer.data(), buffer.size());
            if (n >= 0u)
                co_return static_cast<std::size_t>(n);

            if (is_would_block(errno))
            {
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
            const ssize_t n = ::write(fd_.get(), buffer.data(), buffer.size());
            if (n >= 0u)
                co_return static_cast<std::size_t>(n);

            if (is_would_block(errno))
            {
                co_await exec_.wait_io(fd_.get(), event_type::write);
                continue;
            }

            co_return std::unexpected(error_from_errno());
        }
    }

    task<std::expected<void, std::error_code>> stream::write_all(std::vector<char> buffer) noexcept(false)
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
