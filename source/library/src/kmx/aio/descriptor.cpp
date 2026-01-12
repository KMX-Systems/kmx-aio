#include "kmx/aio/descriptor/epoll.hpp"

namespace kmx::aio
{
    std::expected<void, std::error_code> inet_pton(const int af, const char* const src, void* dst) noexcept
    {
        const int ret = ::inet_pton(af, src, dst);
        if (ret == 0)
            return std::unexpected(error_from_errno(EINVAL));

        if (ret < 0)
            return std::unexpected(error_from_errno());

        return {};
    }
}

namespace kmx::aio::descriptor
{
    file::~file() noexcept
    {
        close();
    }

    file& file::operator=(file&& other) noexcept
    {
        if (this != &other)
        {
            close();
            fd_ = std::exchange(other.fd_, invalid_fd);
        }

        return *this;
    }

    void file::close() noexcept
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
            fd_ = invalid_fd;
        }
    }

    std::expected<file, std::error_code> file::create_socket(const int domain, const int type, const int protocol) noexcept
    {
        const fd_t fd = ::socket(domain, type, protocol);
        if (fd < 0)
            return std::unexpected(error_from_errno());

        return file(fd);
    }

    std::expected<int, std::error_code> file::fcntl(const int cmd, const int arg) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        const int ret = ::fcntl(fd_, cmd, arg);
        if (ret < 0)
            return std::unexpected(error_from_errno());

        return ret;
    }

    std::expected<std::size_t, std::error_code> file::read(void* const buffer, const size_t size) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        const ssize_t ret = ::read(fd_, buffer, size);
        if (ret < 0)
            return std::unexpected(error_from_errno());

        return static_cast<std::size_t>(ret);
    }

    std::expected<std::size_t, std::error_code> file::write(const void* buffer, const size_t size) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        const ssize_t ret = ::write(fd_, buffer, size);
        if (ret < 0)
            return std::unexpected(error_from_errno());

        return static_cast<std::size_t>(ret);
    }

    std::expected<void, std::error_code> file::bind(const struct sockaddr* const addr, const socklen_t addrlen) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        if (::bind(fd_, addr, addrlen) < 0)
            return std::unexpected(error_from_errno());

        return {};
    }

    std::expected<void, std::error_code> file::setsockopt(const int level, const int optname, const void* const optval,
                                                          const socklen_t optlen) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        if (::setsockopt(fd_, level, optname, optval, optlen) < 0)
            return std::unexpected(error_from_errno());

        return {};
    }

    std::expected<void, std::error_code> file::listen(const int backlog) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        if (::listen(fd_, backlog) < 0)
            return std::unexpected(error_from_errno());

        return {};
    }

    std::expected<file, std::error_code> file::accept(struct sockaddr* const addr, socklen_t* const addrlen) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        fd_t client_fd = ::accept(fd_, addr, addrlen);
        if (client_fd < 0)
            return std::unexpected(error_from_errno());

        return file(client_fd);
    }

    std::expected<void, std::error_code> file::connect(const struct sockaddr* const addr, const socklen_t addrlen) noexcept
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

    std::expected<void, std::error_code> file::getsockopt(const int level, const int optname, void* const optval,
                                                          socklen_t* const optlen) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        if (::getsockopt(fd_, level, optname, optval, optlen) < 0)
            return std::unexpected(error_from_errno());

        return {};
    }

    std::expected<void, std::error_code> file::set_as_non_blocking() noexcept
    {
        const auto flags_res = fcntl(F_GETFL, 0);
        if (!flags_res)
            return std::unexpected(flags_res.error());

        const auto set_res = fcntl(F_SETFL, flags_res.value() | O_NONBLOCK);
        if (!set_res)
            return std::unexpected(set_res.error());

        return {};
    }

    std::expected<epoll, std::error_code> epoll::create(const int flags) noexcept
    {
        const fd_t fd = ::epoll_create1(flags);
        if (fd < 0)
            return std::unexpected(error_from_errno());

        return epoll(fd);
    }

    [[nodiscard]] epoll::result_t epoll::add_monitored_fd(const fd_t fd, const event_mask_t events) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        epoll_event ev {};
        ev.events = events;
        ev.data.fd = fd;

        if (::epoll_ctl(get(), EPOLL_CTL_ADD, fd, &ev) < 0)
            return std::unexpected(error_from_errno());

        return {};
    }

    [[nodiscard]] epoll::result_t epoll::modify_events(const fd_t fd, const event_mask_t events) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        epoll_event ev {};
        ev.events = events;
        ev.data.fd = fd;

        if (::epoll_ctl(get(), EPOLL_CTL_MOD, fd, &ev) < 0)
            return std::unexpected(error_from_errno());

        return {};
    }

    [[nodiscard]] epoll::result_t epoll::remove_monitored_fd(const fd_t fd) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        if (::epoll_ctl(get(), EPOLL_CTL_DEL, fd, nullptr) < 0)
            return std::unexpected(error_from_errno());

        return {};
    }

    [[nodiscard]] epoll::result_t epoll::wait_events(std::vector<epoll_event>& events, const int max_events, const int timeout_ms) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        if (max_events <= 0)
            return std::unexpected(error_from_errno(EINVAL));

        events.resize(max_events);
        const int ready = ::epoll_wait(get(), events.data(), max_events, timeout_ms);

        if (ready < 0)
            return std::unexpected(error_from_errno());

        events.resize(ready);
        return {};
    }

    [[nodiscard]] std::expected<std::vector<epoll_event>, std::error_code> epoll::wait_events(const int max_events,
                                                                                              const int timeout_ms) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        if (max_events <= 0)
            return std::unexpected(error_from_errno(EINVAL));

        std::vector<epoll_event> events(max_events);
        const int ready = ::epoll_wait(get(), events.data(), max_events, timeout_ms);

        if (ready < 0)
            return std::unexpected(error_from_errno());

        events.resize(ready);
        return events;
    }
} // namespace kmx::aio
