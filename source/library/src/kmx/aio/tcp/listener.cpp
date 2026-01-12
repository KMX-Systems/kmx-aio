#include "kmx/aio/tcp/listener.hpp"

#include "kmx/logger.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>

namespace kmx::aio::tcp
{
    listener::listener(executor& exec, const std::string_view ip, const std::uint16_t port) noexcept(false): base(exec)
    {
        auto sock_res = descriptor::file::create_socket(AF_INET, SOCK_STREAM, 0);
        if (!sock_res)
            throw std::system_error(sock_res.error());

        fd_ = std::move(sock_res.value());

        const int opt = 1;
        if (auto res = fd_.setsockopt(SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); !res)
            throw std::system_error(res.error());

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_port = ::htons(port);

        if (auto res = aio::inet_pton(AF_INET, std::string(ip).c_str(), &addr.sin_addr); !res)
            throw std::system_error(res.error());

        if (auto res = fd_.bind(reinterpret_cast<sockaddr*>(&addr), sizeof(addr)); !res)
            throw std::system_error(res.error(), "bind failed");
    }

    std::expected<void, std::error_code> listener::listen(int backlog) noexcept
    {
        if (auto res = fd_.set_as_non_blocking(); !res)
            return res;

        if (auto res = fd_.listen(backlog); !res)
            return res;

        return exec_.register_fd(fd_.get());
    }

    task<std::expected<descriptor::file, std::error_code>> listener::accept() noexcept(false)
    {
        for (sockaddr_in client_addr {};;)
        {
            client_addr = {};
            socklen_t len = sizeof(client_addr);

            auto accept_res = fd_.accept(reinterpret_cast<sockaddr*>(&client_addr), &len);
            if (accept_res)
            {
                descriptor::file client_fd = std::move(accept_res.value());

                if (const auto res = client_fd.set_as_non_blocking(); !res)
                    co_return std::unexpected(res.error());

                if (const auto res = exec_.register_fd(client_fd.get()); !res)
                    co_return std::unexpected(res.error());

                logger::log(logger::level::debug, std::source_location::current(), "Accepted connection fd: {}", client_fd.get());
                co_return client_fd;
            }

            if (is_would_block(accept_res.error()))
            {
                co_await exec_.wait_io(fd_.get(), event_type::read);
                continue;
            }

            co_return std::unexpected(accept_res.error());
        }
    }
} // namespace kmx::aio::tcp
