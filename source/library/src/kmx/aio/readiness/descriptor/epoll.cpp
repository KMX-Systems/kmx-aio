#include <kmx/aio/detail/syscalls.hpp>
#include <kmx/aio/readiness/descriptor/epoll.hpp>

namespace kmx::aio::readiness::descriptor
{
    std::expected<epoll, std::error_code> epoll::create(const int flags) noexcept
    {
        const fd_t fd = aio::detail::syscalls::epoll_create1(flags);
        if (fd < 0)
            return std::unexpected(error_from_errno());

        return epoll(fd);
    }

    [[nodiscard]] expected_void_t epoll::add_monitored_fd(const fd_t fd, const event_mask_t events) noexcept
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

    [[nodiscard]] expected_void_t epoll::modify_events(const fd_t fd, const event_mask_t events) noexcept
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

    [[nodiscard]] expected_void_t epoll::remove_monitored_fd(const fd_t fd) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        if (::epoll_ctl(get(), EPOLL_CTL_DEL, fd, nullptr) < 0)
            return std::unexpected(error_from_errno());

        return {};
    }

    [[nodiscard]] expected_size_t epoll::wait_events(std::span<epoll_event> buffer, const int timeout_ms) noexcept
    {
        if (!is_valid())
            return std::unexpected(error_from_errno(EBADF));

        if (buffer.empty())
            return std::unexpected(error_from_errno(EINVAL));

        const int ready = aio::detail::syscalls::epoll_wait(get(), buffer.data(), static_cast<int>(buffer.size()), timeout_ms);
        if (ready < 0)
            return std::unexpected(error_from_errno());

        return static_cast<std::size_t>(ready);
    }

    [[nodiscard]] expected_void_t epoll::wait_events(std::vector<epoll_event>& events, const int max_events,
                                                            const int timeout_ms) noexcept
    {
        if (max_events <= 0)
            return std::unexpected(error_from_errno(EINVAL));

        if (events.size() < static_cast<std::size_t>(max_events))
            events.resize(max_events);

        const auto ready = wait_events(std::span(events).first(static_cast<std::size_t>(max_events)), timeout_ms);
        if (!ready)
            return std::unexpected(ready.error());

        events.resize(*ready);
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
        const int ready = aio::detail::syscalls::epoll_wait(get(), events.data(), max_events, timeout_ms);

        if (ready < 0)
            return std::unexpected(error_from_errno());

        events.resize(ready);
        return events;
    }
} // kmx::aio::readiness::descriptor
