/// @file aio/lifecycle_edges_test.cpp
/// @brief Unit tests for the ownership and shutdown edges the per-component suites do not reach:
///        stream/io_base teardown, the scheduler's exception guard, deferred executor joins, and the
///        descriptor failures that only a wrongly-typed file descriptor produces.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/tcp/stream.hpp>
#include <kmx/aio/file_descriptor.hpp>
#if defined(KMX_AIO_FEATURE_READINESS)
    #include <kmx/aio/readiness/descriptor/epoll.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/readiness/tcp/stream.hpp>
#endif
#include <kmx/aio/scheduler.hpp>
#include <kmx/aio/task.hpp>
#include <kmx/aio/test/executor_runner.hpp>

namespace kmx::aio::test::lifecycle_edges_test
{
    using namespace std::literals::chrono_literals;
    using kmx::aio::test::scoped_runner;
    using kmx::aio::test::wait_for_flag;

    namespace detail
    {
        /// @brief A connected pair of non-blocking sockets, closed on destruction unless released.
        class socket_pair
        {
        public:
            socket_pair() noexcept { valid_ = ::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds_) == 0; }

            socket_pair(const socket_pair&) = delete;
            socket_pair& operator=(const socket_pair&) = delete;

            ~socket_pair() noexcept
            {
                for (int& fd: fds_)
                    if (fd >= 0)
                        ::close(fd);
            }

            [[nodiscard]] bool valid() const noexcept { return valid_; }
            [[nodiscard]] int local() const noexcept { return fds_[0]; }
            [[nodiscard]] int peer() const noexcept { return fds_[1]; }

            /// @brief Gives up ownership of the local end, for handing to a stream.
            [[nodiscard]] int release_local() noexcept { return std::exchange(fds_[0], -1); }

        private:
            int fds_[2] {-1, -1};
            bool valid_ = false;
        };
    } // namespace detail

    // io_base teardown, through the two stream types that derive from it
#if defined(KMX_AIO_FEATURE_READINESS)
    TEST_CASE("a readiness stream unregisters its descriptor on destruction", "[readiness][io_base][lifetime]")
    {
        // ~io_base() is what keeps a closed descriptor from staying in epoll: the fd number is reused by
        // the next open, and a stale registration would then deliver that descriptor's events here.
        detail::socket_pair sockets;
        REQUIRE(sockets.valid());

        readiness::executor exec;
        const int fd = sockets.local();
        REQUIRE(exec.register_fd(fd).has_value());
        REQUIRE(exec.get_stats().total_registrations.load() == 1u);

        {
            readiness::tcp::stream stream {exec, file_descriptor {sockets.release_local()}};
            (void) stream;
        }

        CHECK(exec.get_stats().total_unregistrations.load() == 1u);
    }

    TEST_CASE("a readiness stream skips unregistration for an empty descriptor", "[readiness][io_base][lifetime]")
    {
        readiness::executor exec;

        {
            readiness::tcp::stream stream {exec, file_descriptor {}};
            (void) stream;
        }

        // Nothing was registered, so nothing may be unregistered - the destructor's is_valid() guard.
        CHECK(exec.get_stats().total_unregistrations.load() == 0u);
    }
#endif // KMX_AIO_FEATURE_READINESS

    TEST_CASE("a completion stream tears down without an executor round-trip", "[completion][io_base][lifetime]")
    {
        detail::socket_pair sockets;
        REQUIRE(sockets.valid());

        completion::executor exec;
        {
            completion::tcp::stream stream {exec, file_descriptor {sockets.release_local()}};
            (void) stream;
        }

        SUCCEED("the stream closed its descriptor and left the executor alone");
    }

#if defined(KMX_AIO_FEATURE_READINESS)
    TEST_CASE("a stream outliving its executor does not touch it", "[readiness][io_base][lifetime]")
    {
        // io_base holds a weak lifetime token precisely so that this ordering is safe: the destructor
        // must notice the executor is gone rather than call unregister_fd on freed memory.
        detail::socket_pair sockets;
        REQUIRE(sockets.valid());

        auto exec = std::make_unique<readiness::executor>();
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        readiness::tcp::stream stream {*exec, file_descriptor {sockets.release_local()}};
        exec.reset();

        SUCCEED("the stream's destructor runs after the executor is gone");
    }
#endif // KMX_AIO_FEATURE_READINESS

    // scheduler
    TEST_CASE("the scheduler survives a task that throws", "[core][scheduler][exception]")
    {
        // A worker that let an exception escape would take the thread with it and silently shrink the
        // pool, so the guard has to swallow it and keep serving the queue.
        std::atomic_bool after_throw {false};

        {
            scheduler sched {1u};

            sched.spawn([]() { throw std::runtime_error {"thrown from a scheduled task"}; });
            sched.spawn([&after_throw]() { after_throw.store(true, std::memory_order_release); });

            REQUIRE(wait_for_flag(after_throw, 5s));
        }

        CHECK(after_throw.load(std::memory_order_acquire));
    }

    TEST_CASE("the scheduler runs work across several workers", "[core][scheduler]")
    {
        std::atomic_int completed {0};

        {
            scheduler sched {2u};
            for (int i = 0; i < 8; ++i)
                sched.spawn([&completed]() { completed.fetch_add(1, std::memory_order_acq_rel); });

            const auto deadline = std::chrono::steady_clock::now() + 5s;
            while ((completed.load(std::memory_order_acquire) < 8) && (std::chrono::steady_clock::now() < deadline))
                std::this_thread::sleep_for(1ms);
        }

        CHECK(completed.load(std::memory_order_acquire) == 8);
    }

    // shutdown from a thread the executor owns
#if defined(KMX_AIO_FEATURE_READINESS)
    TEST_CASE("a readiness executor stopped from its own task is not leaked", "[readiness][executor][lifecycle]")
    {
        // Regression: stop() called from a task was resumed on a scheduler worker, and the worker joined
        // the I/O thread itself. run() then found the thread already taken and returned while that
        // shutdown was still running, so the caller dropped its reference first and the last one was
        // released on the worker - running ~executor(), and ~scheduler()'s join, on a thread the
        // scheduler was about to join. That aborted with "Resource deadlock avoided" when it lost the
        // race and leaked the executor and both its threads when it won.
        //
        // What has to hold: once run() returns, no thread the executor owns still holds a reference, so
        // releasing the caller's is what destroys it.
        std::weak_ptr<readiness::executor> watch;
        std::atomic_bool ran {false};

        {
            auto exec = std::make_shared<readiness::executor>();
            watch = exec;

            auto body = [exec, &ran]() -> task<void>
            {
                const auto waited = co_await exec->async_timeout(2'000'000u);
                (void) waited;
                ran.store(true, std::memory_order_release);
                exec->stop();
            };
            exec->spawn(body());
            exec->run();
            exec->stop();
        }

        CHECK(ran.load(std::memory_order_acquire));
        CHECK(watch.expired());
    }

    TEST_CASE("repeatedly stopping from a task does not accumulate threads", "[readiness][executor][lifecycle]")
    {
        // The leak above was invisible in a single iteration and obvious across several: each one left
        // an I/O thread and a scheduler worker behind.
        for (int i = 0; i < 8; ++i)
        {
            std::weak_ptr<readiness::executor> watch;
            {
                auto exec = std::make_shared<readiness::executor>();
                watch = exec;

                auto body = [exec]() -> task<void>
                {
                    const auto waited = co_await exec->async_timeout(1'000'000u);
                    (void) waited;
                    exec->stop();
                };
                exec->spawn(body());
                exec->run();
                exec->stop();
            }

            CAPTURE(i);
            REQUIRE(watch.expired());
        }
    }
#endif // KMX_AIO_FEATURE_READINESS

    // deferred executor joins - stop() called from inside the I/O thread
    TEST_CASE("a completion executor stopped from its own thread is joined by the caller", "[completion][executor][lifecycle]")
    {
        // stop() cannot join the thread it is running on, so it leaves the join to whoever calls next.
        // That second path is what a coroutine calling exec.stop() exercises.
        completion::executor exec;
        std::atomic_bool ran {false};

        auto body = [&exec, &ran]() -> task<void>
        {
            const auto waited = co_await exec.async_timeout(5'000'000u); // 5ms
            (void) waited;
            ran.store(true, std::memory_order_release);
            exec.stop();
        };
        exec.spawn(body());
        exec.run();

        CHECK(ran.load(std::memory_order_acquire));

        // run() returned, but the self-stop deferred the join; a further stop() has to finish it rather
        // than leave a joinable thread behind for the destructor.
        exec.stop();
        SUCCEED("the deferred join completed");
    }

#if defined(KMX_AIO_FEATURE_READINESS)
    TEST_CASE("a readiness executor stopped from its own thread is joined by the caller", "[readiness][executor][lifecycle]")
    {
        auto exec = std::make_shared<readiness::executor>();
        std::atomic_bool ran {false};

        auto body = [exec, &ran]() -> task<void>
        {
            const auto waited = co_await exec->async_timeout(5'000'000u);
            (void) waited;
            ran.store(true, std::memory_order_release);
            exec->stop();
        };
        exec->spawn(body());
        exec->run();

        CHECK(ran.load(std::memory_order_acquire));
        exec->stop();
        SUCCEED("the deferred join completed");
    }

    // descriptor failures that need a wrongly-typed file descriptor
    TEST_CASE("epoll_wait fails on a descriptor that is not an epoll instance", "[readiness][epoll][wait][error]")
    {
        // Both wait_events overloads guard on is_valid() and on max_events, and then have a third
        // failure to report: the descriptor passing those checks but not being an epoll fd.
        int fds[2] {-1, -1};
        REQUIRE(::pipe(fds) == 0);

        std::vector<::epoll_event> events;
        {
            readiness::descriptor::epoll impostor {fds[0]};
            const auto out_param = impostor.wait_events(events, 4, 0);
            REQUIRE_FALSE(out_param.has_value());
            CHECK(out_param.error() == std::errc::invalid_argument);

            const auto returning = impostor.wait_events(4, 0);
            REQUIRE_FALSE(returning.has_value());
            CHECK(returning.error() == std::errc::invalid_argument);
        }

        ::close(fds[1]);
    }
#endif // KMX_AIO_FEATURE_READINESS

    TEST_CASE("accept reports an address family it cannot represent", "[core][file_descriptor][accept][error]")
    {
        // The convenience overload maps the peer address into ip_address_owned_t, which covers IPv4 and
        // IPv6 only. A Unix-domain peer has no such representation, and the default arm says so rather
        // than handing back an uninitialised address.
        const std::string path {"/tmp/kmx-aio-accept-family-test.sock"};
        ::unlink(path.c_str());

        auto listener = file_descriptor::create_socket(AF_UNIX, SOCK_STREAM, 0);
        REQUIRE(listener.has_value());

        ::sockaddr_un address {};
        address.sun_family = AF_UNIX;
        std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1u);

        REQUIRE(listener->bind(reinterpret_cast<const ::sockaddr*>(&address), sizeof(address)).has_value());
        REQUIRE(listener->listen(1).has_value());

        auto client = file_descriptor::create_socket(AF_UNIX, SOCK_STREAM, 0);
        REQUIRE(client.has_value());
        REQUIRE(client->connect(reinterpret_cast<const ::sockaddr*>(&address), sizeof(address)).has_value());

        ip_address_owned_t peer_ip {};
        port_t peer_port {};
        const auto accepted = listener->accept(peer_ip, peer_port);

        REQUIRE_FALSE(accepted.has_value());
        CHECK(accepted.error() == error_from_errno(EAFNOSUPPORT));

        ::unlink(path.c_str());
    }
} // namespace kmx::aio::test::lifecycle_edges_test
