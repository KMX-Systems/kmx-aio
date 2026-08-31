/// @file aio/benchmark/baseline_cases.cpp
/// @brief Raw-syscall reference points the executor numbers are read against.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/benchmark/cases.hpp>

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace kmx::aio::benchmark
{
    namespace baseline_detail
    {
        /// @brief An anonymous socket pair, closed on destruction.
        struct socket_pair
        {
            int fd[2] {-1, -1};

            explicit socket_pair(const int flags) noexcept { valid = ::socketpair(AF_UNIX, SOCK_STREAM | flags, 0, fd) == 0; }

            ~socket_pair() noexcept
            {
                for (const int f: fd)
                    if (f >= 0)
                        ::close(f);
            }

            socket_pair(const socket_pair&) = delete;
            socket_pair& operator=(const socket_pair&) = delete;

            bool valid {};
        };

        /// @brief Writes one byte, ignoring short writes that a socketpair cannot produce.
        static void ping(const int fd) noexcept
        {
            const char byte {};
            const auto written = ::write(fd, &byte, 1u);
            keep(written);
        }

        /// @brief Reads one byte, retrying while the descriptor is not ready.
        static void drain(const int fd) noexcept
        {
            char byte {};
            while (::read(fd, &byte, 1u) < 0)
                if (errno != EINTR)
                    return;
        }
    } // namespace baseline_detail

    static result bench_epoll_rtt(const double scale)
    {
        const auto iterations = scaled(200'000u, scale);
        baseline_detail::socket_pair pair {SOCK_NONBLOCK};
        if (!pair.valid)
            return skipped("baseline/socketpair_rtt (epoll, 1 thread)", "socketpair failed");

        const int epoll_fd = ::epoll_create1(0);
        if (epoll_fd < 0)
            return skipped("baseline/socketpair_rtt (epoll, 1 thread)", "epoll_create1 failed");

        for (const int fd: pair.fd)
        {
            epoll_event ev {};
            ev.events = EPOLLIN | EPOLLET;
            ev.data.fd = fd;
            keep(::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev));
        }

        std::vector<double> samples {};
        samples.reserve(iterations);
        epoll_event event {};

        for (std::size_t i {}; i != iterations; ++i)
        {
            const auto start = clock_t::now();

            baseline_detail::ping(pair.fd[0]);
            keep(::epoll_wait(epoll_fd, &event, 1, -1));
            baseline_detail::drain(pair.fd[1]);

            baseline_detail::ping(pair.fd[1]);
            keep(::epoll_wait(epoll_fd, &event, 1, -1));
            baseline_detail::drain(pair.fd[0]);

            samples.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count()));
        }

        ::close(epoll_fd);
        auto out = from_samples("baseline/socketpair_rtt (epoll, 1 thread)", samples);
        out.note = "floor for a thread-per-core reactor: 2 x (write + epoll_wait + read), no handoff";
        return out;
    }

    static result bench_epoll_rtt_eagain(const double scale)
    {
        const auto iterations = scaled(200'000u, scale);
        baseline_detail::socket_pair pair {SOCK_NONBLOCK};
        if (!pair.valid)
            return skipped("baseline/socketpair_rtt (epoll + EAGAIN probe)", "socketpair failed");

        const int epoll_fd = ::epoll_create1(0);
        if (epoll_fd < 0)
            return skipped("baseline/socketpair_rtt (epoll + EAGAIN probe)", "epoll_create1 failed");

        for (const int fd: pair.fd)
        {
            epoll_event ev {};
            ev.events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLET;
            ev.data.fd = fd;
            keep(::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev));
        }

        std::vector<double> samples {};
        samples.reserve(iterations);
        epoll_event events[8] {};

        // The same syscall sequence a readiness coroutine performs: try the read first, wait only once
        // it reports EAGAIN, then read again. Two more system calls per round trip than the plain
        // baseline above, and the honest floor for the executor's own pattern.
        const auto half_trip = [&](const int write_fd, const int read_fd) noexcept
        {
            baseline_detail::ping(write_fd);
            char byte {};
            while (::read(read_fd, &byte, 1u) < 0)
                keep(::epoll_wait(epoll_fd, events, 8, -1));
        };

        for (std::size_t i {}; i != iterations; ++i)
        {
            const auto start = clock_t::now();
            half_trip(pair.fd[0], pair.fd[1]);
            half_trip(pair.fd[1], pair.fd[0]);
            samples.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count()));
        }

        ::close(epoll_fd);
        auto out = from_samples("baseline/socketpair_rtt (epoll + EAGAIN probe)", samples);
        out.note = "the executor's exact syscall pattern without the executor: 2 x (write + read + epoll_wait + read)";
        return out;
    }

    static result bench_thread_handoff_rtt(const double scale)
    {
        const auto iterations = scaled(100'000u, scale);
        baseline_detail::socket_pair pair {0};
        if (!pair.valid)
            return skipped("baseline/socketpair_rtt (2 threads, blocking)", "socketpair failed");

        const int peer_fd = pair.fd[1];
        std::jthread peer {[peer_fd, iterations]() noexcept
                           {
                               for (std::size_t i {}; i != iterations; ++i)
                               {
                                   baseline_detail::drain(peer_fd);
                                   baseline_detail::ping(peer_fd);
                               }
                           }};

        std::vector<double> samples {};
        samples.reserve(iterations);

        for (std::size_t i {}; i != iterations; ++i)
        {
            const auto start = clock_t::now();
            baseline_detail::ping(pair.fd[0]);
            baseline_detail::drain(pair.fd[0]);
            samples.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count()));
        }

        peer.join();
        auto out = from_samples("baseline/socketpair_rtt (2 threads, blocking)", samples);
        out.note = "cost of crossing a thread boundary and back: wake-up plus context switch";
        return out;
    }

    static result bench_heap_alloc(const double scale)
    {
        const auto iterations = scaled(20'000'000u, scale);
        const auto start = clock_t::now();
        for (std::size_t i {}; i != iterations; ++i)
        {
            void* const p = ::operator new(256u);
            keep(p);
            ::operator delete(p, 256u);
        }

        const auto elapsed = clock_t::now() - start;
        return from_total("baseline/operator_new+delete (256 B)", iterations, elapsed);
    }

    void register_baseline_cases(registry& reg) noexcept(false)
    {
        reg.add("baseline/heap_alloc", bench_heap_alloc);
        reg.add("baseline/epoll_rtt", bench_epoll_rtt);
        reg.add("baseline/epoll_rtt_eagain", bench_epoll_rtt_eagain);
        reg.add("baseline/thread_handoff_rtt", bench_thread_handoff_rtt);
    }

} // namespace kmx::aio::benchmark
