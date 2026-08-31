/// @file aio/file_descriptor.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/file_descriptor.hpp>

#include <kmx/aio/detail/syscalls.hpp>

namespace kmx::aio
{
    expected_void_t inet_pton(const int af, const char* const src, void* dst) noexcept
    {
        const int ret = ::inet_pton(af, src, dst);
        if (ret == 0)
            return std::unexpected(error_from_errno(EINVAL));

        if (ret < 0)
            return std::unexpected(error_from_errno());

        return {};
    }

    file_descriptor::~file_descriptor() noexcept
    {
        close();
    }

    file_descriptor& file_descriptor::operator=(file_descriptor&& other) noexcept
    {
        if (this != &other)
        {
            close();
            fd_ = std::exchange(other.fd_, invalid_fd);
        }

        return *this;
    }

    void file_descriptor::close() noexcept
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
            fd_ = invalid_fd;
        }
    }

    file_descriptor::expected_t file_descriptor::create_socket(const int domain, const int type, const int protocol) noexcept
    {
        const fd_t fd = detail::syscalls::socket(domain, type, protocol);
        if (fd < 0)
            return std::unexpected(error_from_errno());

        return file_descriptor(fd);
    }

    expected_int_t file_descriptor::fcntl(const int cmd, const int arg) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        const int ret = detail::syscalls::fcntl(fd_, cmd, arg);
        if (ret < 0)
            return std::unexpected(error_from_errno());

        return ret;
    }

    expected_size_t file_descriptor::read(void* const buffer, const size_t size) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        const ssize_t ret = ::read(fd_, buffer, size);
        if (ret < 0)
            return std::unexpected(error_from_errno());

        return static_cast<std::size_t>(ret);
    }

    expected_size_t file_descriptor::write(const void* buffer, const size_t size) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        const ssize_t ret = ::write(fd_, buffer, size);
        if (ret < 0)
            return std::unexpected(error_from_errno());

        return static_cast<std::size_t>(ret);
    }

    expected_void_t file_descriptor::bind(const struct sockaddr* const addr, const ::socklen_t addrlen) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        if (::bind(fd_, addr, addrlen) < 0)
            return std::unexpected(error_from_errno());

        return {};
    }

    expected_void_t file_descriptor::bind(const ip_address_t ip, const port_t port) noexcept
    {
        const auto addr = make_socket_address(ip, port);
        // LCOV_EXCL_START
        // make_socket_address fills a sockaddr from a variant that is either IPv4 or IPv6 and cannot
        // fail today; it returns std::expected so that a future address family may. Forwarding the
        // error is what keeps this call site correct when one does.
        if (!addr)
            return std::unexpected(addr.error());
        // LCOV_EXCL_STOP

        return bind(reinterpret_cast<const sockaddr*>(&addr->storage), addr->length);
    }

    expected_void_t file_descriptor::setsockopt(const int level, const int optname, const void* const optval,
                                                const ::socklen_t optlen) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        if (::setsockopt(fd_, level, optname, optval, optlen) < 0)
            return std::unexpected(error_from_errno());

        return {};
    }

    expected_void_t file_descriptor::listen(const int backlog) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        if (::listen(fd_, backlog) < 0)
            return std::unexpected(error_from_errno());

        return {};
    }

    file_descriptor::expected_t file_descriptor::accept(struct sockaddr* const addr, ::socklen_t* const addrlen) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        fd_t client_fd = ::accept(fd_, addr, addrlen);
        if (client_fd < 0)
            return std::unexpected(error_from_errno());

        return file_descriptor(client_fd);
    }

    file_descriptor::expected_t file_descriptor::accept(ip_address_owned_t& out_ip, port_t& out_port) noexcept
    {
        sockaddr_storage storage {};
        ::socklen_t length = sizeof(storage);

        auto file_res = accept(reinterpret_cast<sockaddr*>(&storage), &length);
        if (!file_res)
            return file_res;

        switch (storage.ss_family)
        {
            case AF_INET:
            {
                auto* addr4 = reinterpret_cast<::sockaddr_in*>(&storage);
                ipv4::storage_t ip4 {};
                std::memcpy(ip4.data(), &addr4->sin_addr, ip4.size());
                out_ip = ip4;
                out_port = ::ntohs(addr4->sin_port);
                break;
            }
            case AF_INET6:
            {
                auto* addr6 = reinterpret_cast<sockaddr_in6*>(&storage);
                ipv6::storage_t ip6 {};
                std::memcpy(ip6.data(), &addr6->sin6_addr, ip6.size());
                out_ip = ip6;
                out_port = ::ntohs(addr6->sin6_port);
                break;
            }
            default:
                // Invalid or unsupported family
                return std::unexpected(error_from_errno(EAFNOSUPPORT));
        }

        return file_res;
    }

    expected_void_t file_descriptor::connect(const struct sockaddr* const addr, const ::socklen_t addrlen) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        if (::connect(fd_, addr, addrlen) < 0)
        {
            // EINPROGRESS is not an error for non-blocking sockets
            if (errno != EINPROGRESS)
                return std::unexpected(error_from_errno());
        }

        return {};
    }

    expected_void_t file_descriptor::connect(const ip_address_t ip, const port_t port) noexcept
    {
        const auto addr = make_socket_address(ip, port);
        // LCOV_EXCL_START
        // make_socket_address fills a sockaddr from a variant that is either IPv4 or IPv6 and cannot
        // fail today; it returns std::expected so that a future address family may. Forwarding the
        // error is what keeps this call site correct when one does.
        if (!addr)
            return std::unexpected(addr.error());
        // LCOV_EXCL_STOP

        return connect(reinterpret_cast<const sockaddr*>(&addr->storage), addr->length);
    }

    expected_void_t file_descriptor::getsockopt(const int level, const int optname, void* const optval, ::socklen_t* const optlen) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        if (::getsockopt(fd_, level, optname, optval, optlen) < 0)
            return std::unexpected(error_from_errno());

        return {};
    }

    expected_void_t file_descriptor::set_as_non_blocking() noexcept
    {
        const auto flags_res = fcntl(F_GETFL, 0);
        if (!flags_res)
            return std::unexpected(flags_res.error());

        const auto set_res = fcntl(F_SETFL, flags_res.value() | O_NONBLOCK);
        if (!set_res)
            return std::unexpected(set_res.error());

        return {};
    }
} // namespace kmx::aio
