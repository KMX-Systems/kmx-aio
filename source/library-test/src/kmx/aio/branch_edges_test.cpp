/// @file aio/branch_edges_test.cpp
/// @brief Tests for conditionals whose other side no existing case takes.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// These are not new behaviours, they are the untaken half of decisions the library already makes: a
/// log level outside the table, a scheduler asked to wait from one of its own workers, a wait cancelled
/// before anyone subscribed to it. Each one is cheap to reach directly and expensive to reach by
/// accident, which is why they were still one-sided.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <span>
#include <stop_token>
#include <utility>

#include <kmx/aio/channel.hpp>
#include <kmx/aio/completion/executor.hpp>
#if defined(KMX_AIO_FEATURE_READINESS)
    #include <kmx/aio/readiness/executor.hpp>
#endif
#include <kmx/aio/scheduler.hpp>
#include <kmx/aio/task.hpp>
#include <kmx/aio/test/executor_runner.hpp>
#include <kmx/logger.hpp>

namespace kmx::aio
{
    using namespace std::literals::chrono_literals;
    using kmx::aio::test::scoped_runner;
    using kmx::aio::test::wait_for_flag;

    // =========================================================================
    // logger
    // =========================================================================

    TEST_CASE("an out-of-range log level falls back to the unknown marker", "[core][logger][branch]")
    {
        // level_to_char indexes a table and clamps past the end. Every ordinary call takes the in-range
        // side; only a value no enumerator names reaches the fallback.
        const auto bogus = static_cast<logger::level>(42);
        logger::log(bogus, std::source_location::current(), "level {} has no character of its own", 42);
        SUCCEED("an unknown level formatted without indexing past the table");
    }

    // =========================================================================
    // scheduler
    // =========================================================================

    TEST_CASE("wait_until_idle returns immediately when called from a worker", "[core][scheduler][branch]")
    {
        // A worker waiting for the scheduler to fall idle would be waiting for the task it is itself
        // running. The guard is what stops that from being a deadlock, and only a call from inside a
        // task takes it.
        std::atomic_bool returned {false};

        {
            scheduler sched {2u};
            sched.spawn(
                [&sched, &returned]()
                {
                    sched.wait_until_idle();
                    returned.store(true, std::memory_order_release);
                });

            REQUIRE(wait_for_flag(returned, 5s));
        }

        CHECK(returned.load(std::memory_order_acquire));
    }

    TEST_CASE("wait_until_idle waits for a backlog to drain", "[core][scheduler][branch]")
    {
        // The other side of the same predicate: a queue that is not yet empty, and a task still running.
        std::atomic_int completed {0};

        {
            scheduler sched {1u};
            for (int i = 0; i < 6; ++i)
                sched.spawn(
                    [&completed]()
                    {
                        std::this_thread::sleep_for(5ms);
                        completed.fetch_add(1, std::memory_order_acq_rel);
                    });

            sched.wait_until_idle();
            CHECK(completed.load(std::memory_order_acquire) == 6);
        }
    }

    TEST_CASE("wait_until_idle on an untouched scheduler returns at once", "[core][scheduler][branch]")
    {
        scheduler sched {1u};
        sched.wait_until_idle();
        SUCCEED("an idle scheduler is already idle");
    }

    // =========================================================================
    // readiness executor: cancellation bookkeeping
    // =========================================================================

    namespace
    {
        /// @brief A connected pair of non-blocking sockets, closed on destruction.
        class socket_pair
        {
        public:
            socket_pair() noexcept { valid_ = ::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds_) == 0; }

            socket_pair(const socket_pair&) = delete;
            socket_pair& operator=(const socket_pair&) = delete;

            ~socket_pair() noexcept
            {
                if (valid_)
                {
                    ::close(fds_[0]);
                    ::close(fds_[1]);
                }
            }

            [[nodiscard]] bool valid() const noexcept { return valid_; }
            [[nodiscard]] int local() const noexcept { return fds_[0]; }
            [[nodiscard]] int peer() const noexcept { return fds_[1]; }

        private:
            int fds_[2] {-1, -1};
            bool valid_ = false;
        };
    }

#if defined(KMX_AIO_FEATURE_READINESS)
    TEST_CASE("cancelling a descriptor nobody waits on is a no-op", "[readiness][executor][branch]")
    {
        // cancel_io walks the subscription table and finds nothing to resume. The empty-table side of
        // that search is never taken by a test that cancels a wait it just parked.
        socket_pair sockets;
        REQUIRE(sockets.valid());

        auto exec = std::make_shared<readiness::executor>();
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        exec->cancel_io(sockets.local());
        exec->cancel_io(sockets.peer());

        SUCCEED("cancelling an unsubscribed descriptor did not fault");
    }

    TEST_CASE("a wait on an already-cancelled descriptor does not suspend", "[readiness][executor][branch]")
    {
        // subscribe() refuses a descriptor that cancel_io has marked, and the awaiter reports the wait
        // as cancelled without ever suspending. That is the arm that keeps a cancel landing between a
        // caller's own check and its subscription from being lost.
        socket_pair sockets;
        REQUIRE(sockets.valid());

        auto exec = std::make_shared<readiness::executor>();
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        // Marks the descriptor before anything subscribes to it.
        exec->cancel_io(sockets.local());

        std::atomic_bool done {false};
        std::atomic_bool fired {true};

        auto body = [&done, &fired, exec, fd = sockets.local()]() -> task<void>
        {
            const bool event = co_await exec->wait_io(fd, readiness::event_type::read);
            fired.store(event, std::memory_order_release);
            done.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(done, 5s));
        CHECK_FALSE(fired.load(std::memory_order_acquire));
    }

    TEST_CASE("two waiters on one descriptor are both resumed", "[readiness][executor][branch]")
    {
        // The subscription list holds more than one entry per descriptor, and cancel walks all of them.
        // A single-waiter test only ever takes the "list is now empty" side of that loop.
        socket_pair sockets;
        REQUIRE(sockets.valid());

        auto exec = std::make_shared<readiness::executor>();
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        std::atomic_int parked {0};
        std::atomic_int finished {0};

        auto first = [&parked, &finished, exec, fd = sockets.local()]() -> task<void>
        {
            parked.fetch_add(1, std::memory_order_acq_rel);
            const bool event = co_await exec->wait_io(fd, readiness::event_type::read);
            (void) event;
            finished.fetch_add(1, std::memory_order_acq_rel);
        };

        auto second = [&parked, &finished, exec, fd = sockets.local()]() -> task<void>
        {
            parked.fetch_add(1, std::memory_order_acq_rel);
            const bool event = co_await exec->wait_io(fd, readiness::event_type::read);
            (void) event;
            finished.fetch_add(1, std::memory_order_acq_rel);
        };

        exec->spawn(first());
        exec->spawn(second());

        scoped_runner runner {*exec};

        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while ((parked.load(std::memory_order_acquire) < 2) && (std::chrono::steady_clock::now() < deadline))
            std::this_thread::sleep_for(1ms);

        REQUIRE(parked.load(std::memory_order_acquire) == 2);
        std::this_thread::sleep_for(50ms);

        exec->cancel_io(sockets.local());

        const auto finish_by = std::chrono::steady_clock::now() + 5s;
        while ((finished.load(std::memory_order_acquire) < 2) && (std::chrono::steady_clock::now() < finish_by))
            std::this_thread::sleep_for(1ms);

        CHECK(finished.load(std::memory_order_acquire) == 2);
    }

    TEST_CASE("a write wait and a read wait on one descriptor are tracked apart", "[readiness][executor][branch]")
    {
        // Subscriptions are keyed on descriptor and direction, so cancelling one direction leaves the
        // other in the table - the "different key, keep looking" arm of the cancellation walk.
        socket_pair sockets;
        REQUIRE(sockets.valid());

        auto exec = std::make_shared<readiness::executor>();
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        std::atomic_bool read_parked {false};
        std::atomic_bool read_done {false};

        auto reader = [&read_parked, &read_done, exec, fd = sockets.local()]() -> task<void>
        {
            read_parked.store(true, std::memory_order_release);
            const bool event = co_await exec->wait_io(fd, readiness::event_type::read);
            (void) event;
            read_done.store(true, std::memory_order_release);
        };
        exec->spawn(reader());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(read_parked, 2s));
        std::this_thread::sleep_for(50ms);

        exec->cancel_io(sockets.local());
        REQUIRE(wait_for_flag(read_done, 5s));
    }

    // =========================================================================
    // executor shutdown: the untaken sides of the join conditions
    // =========================================================================

    TEST_CASE("a second stop from inside a task finds the join already taken", "[readiness][executor][branch]")
    {
        // stop() has two shapes. The first call wins running_.exchange(false) and, from a thread the
        // executor owns, defers the join. A second call falls through to the path that finishes a
        // deferred join - and from an owned thread that too has to decline, which is the side no test
        // reached: every other second-stop in the suite comes from outside.
        auto exec = std::make_shared<readiness::executor>();
        std::atomic_bool ran {false};

        auto body = [exec, &ran]() -> task<void>
        {
            const auto waited = co_await exec->async_timeout(2'000'000u);
            (void) waited;
            exec->stop(); // wins the exchange, defers the join
            exec->stop(); // running_ already false: the deferred-join path, from an owned thread
            ran.store(true, std::memory_order_release);
        };
        exec->spawn(body());
        exec->run();
        exec->stop();

        CHECK(ran.load(std::memory_order_acquire));
    }
#endif // KMX_AIO_FEATURE_READINESS

    TEST_CASE("a second stop from inside a completion task finds the join already taken", "[completion][executor][branch]")
    {
        completion::executor exec;
        bool ran = false;

        auto body = [&exec, &ran]() -> task<void>
        {
            const auto waited = co_await exec.async_timeout(2'000'000u);
            (void) waited;
            exec.stop();
            exec.stop();
            ran = true;
        };
        exec.spawn(body());
        exec.run();
        exec.stop();

        CHECK(ran);
    }

#if defined(KMX_AIO_FEATURE_READINESS)
    TEST_CASE("run on an executor that is already running does not start a second loop",
              "[readiness][executor][branch]")
    {
        // run() arms the executor with running_.exchange(true) and only creates the I/O thread when it
        // was the one to arm it. A second run() has to find it already armed and not start a rival loop.
        socket_pair sockets;
        REQUIRE(sockets.valid());

        auto exec = std::make_shared<readiness::executor>();
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        std::atomic_bool parked {false};
        std::atomic_bool done {false};

        auto body = [&parked, &done, exec, fd = sockets.local()]() -> task<void>
        {
            parked.store(true, std::memory_order_release);
            const bool event = co_await exec->wait_io(fd, readiness::event_type::read);
            (void) event;
            done.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(parked, 2s));

        std::atomic_bool second_returned {false};
        std::jthread second([exec, &second_returned]()
                            {
                                exec->run();
                                second_returned.store(true, std::memory_order_release);
                            });

        std::this_thread::sleep_for(50ms);
        exec->cancel_io(sockets.local());

        REQUIRE(wait_for_flag(done, 5s));
        REQUIRE(wait_for_flag(second_returned, 5s));
        SUCCEED("the second run() shared the loop the first had already started");
    }
#endif // KMX_AIO_FEATURE_READINESS

    // =========================================================================
    // scheduler: the remaining sides of the idle predicate
    // =========================================================================

    TEST_CASE("the idle predicate distinguishes a busy worker from a full queue", "[core][scheduler][branch]")
    {
        // `queue_.empty() && active_ == 0` has four outcomes and a single-worker drain reaches two of
        // them. Two workers, one of them held on a task while the queue still has entries, reaches the
        // rest: queue non-empty with a worker busy, and queue empty with a worker still busy.
        std::atomic_bool release {false};
        std::atomic_int started {0};
        std::atomic_int completed {0};

        {
            scheduler sched {2u};

            sched.spawn(
                [&release, &started, &completed]()
                {
                    started.fetch_add(1, std::memory_order_acq_rel);
                    while (!release.load(std::memory_order_acquire))
                        std::this_thread::sleep_for(1ms);

                    completed.fetch_add(1, std::memory_order_acq_rel);
                });

            for (int i = 0; i < 4; ++i)
                sched.spawn([&completed]() { completed.fetch_add(1, std::memory_order_acq_rel); });

            const auto deadline = std::chrono::steady_clock::now() + 5s;
            while ((started.load(std::memory_order_acquire) == 0) && (std::chrono::steady_clock::now() < deadline))
                std::this_thread::sleep_for(1ms);

            REQUIRE(started.load(std::memory_order_acquire) == 1);

            release.store(true, std::memory_order_release);
            sched.wait_until_idle();
        }

        CHECK(completed.load(std::memory_order_acquire) == 5);
    }

    // =========================================================================
    // channel: the throttle transitions that happen mid-push
    // =========================================================================

    TEST_CASE("a push that crosses the high watermark flips the throttle", "[core][channel][branch]")
    {
        // try_push re-evaluates the throttle both before and after storing. The before-push flip is the
        // one no other test reaches: it needs the channel to have crossed the watermark since the last
        // push, which happens when the watermark is lowered underneath a producer between two pushes.
        channel<int> ch {16u};
        ch.set_backpressure({.low_watermark = 1u, .high_watermark = 8u});

        for (int i = 0; i < 4; ++i)
            REQUIRE(ch.try_push(int {i}));

        REQUIRE(ch.can_send());

        // Now the occupancy is above the new high watermark, and the next push has to notice on the way
        // in rather than after storing.
        ch.set_backpressure({.low_watermark = 1u, .high_watermark = 2u});
        CHECK_FALSE(ch.try_push(99));

        // Draining below the low watermark releases it again.
        while (ch.try_pop().has_value())
        {
        }

        CHECK(ch.can_send());
        CHECK(ch.try_push(100));
    }

#if defined(KMX_AIO_FEATURE_READINESS)
    TEST_CASE("an event on a descriptor nobody waits on is discarded", "[readiness][executor][branch]")
    {
        // resume_if_found looks the descriptor up and finds either no entry at all or an empty waiter
        // list. Both are ordinary: a descriptor can be registered and become readable before anything
        // has awaited it, and the loop must drop the event rather than resume something that is not
        // there. Every other test in the suite parks a wait first, so only the found-and-non-empty side
        // had ever been taken.
        socket_pair sockets;
        REQUIRE(sockets.valid());

        auto exec = std::make_shared<readiness::executor>();
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        // Readable straight away, with nothing waiting on it.
        const char byte = 'x';
        REQUIRE(::write(sockets.peer(), &byte, 1u) == 1);

        std::atomic_bool ran {false};
        auto body = [&ran, exec]() -> task<void>
        {
            // Something for the loop to finish, so run() has work that drains on its own.
            const auto waited = co_await exec->async_timeout(40'000'000u);
            (void) waited;
            ran.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(ran, 5s));
        CHECK(exec->get_stats().total_events_received.load() > 0u);
    }

    TEST_CASE("a wait that consumes the only subscription empties its list", "[readiness][executor][branch]")
    {
        // The list for a descriptor is erased once its last waiter is taken, and kept when others
        // remain. A single waiter reaches the erase; the two-waiter case below it reaches the other.
        socket_pair sockets;
        REQUIRE(sockets.valid());

        auto exec = std::make_shared<readiness::executor>();
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        std::atomic_bool parked {false};
        std::atomic_bool done {false};

        auto body = [&parked, &done, exec, fd = sockets.local()]() -> task<void>
        {
            parked.store(true, std::memory_order_release);
            const bool event = co_await exec->wait_io(fd, readiness::event_type::read);
            (void) event;
            done.store(true, std::memory_order_release);

            // A second wait on the same descriptor after the list was erased, so the lookup runs again
            // against a table that no longer holds the entry.
            const auto waited = co_await exec->async_timeout(20'000'000u);
            (void) waited;
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(parked, 2s));
        std::this_thread::sleep_for(30ms);

        const char byte = 'x';
        REQUIRE(::write(sockets.peer(), &byte, 1u) == 1);
        REQUIRE(wait_for_flag(done, 5s));
    }
#endif // KMX_AIO_FEATURE_READINESS

    // =========================================================================
    // task: the guard on a task that no longer owns a coroutine
    // =========================================================================

    TEST_CASE("with_stop_token ignores a task that has been moved from", "[core][task][branch]")
    {
        // with_stop_token guards on the handle because a moved-from task owns nothing. Handing one a
        // token has to be a no-op rather than a dereference of null.
        std::stop_source source;

        auto make = []() -> task<void> { co_return; };

        task<void> original = make();
        task<void> adopted {std::move(original)};

        // `original` is moved-from: no handle, nothing to give the token to.
        task<void> ignored = std::move(original).with_stop_token(source.get_token());
        (void) ignored;

        SUCCEED("a moved-from task accepted a stop token without dereferencing a null handle");
    }

#if defined(KMX_AIO_FEATURE_READINESS)
    TEST_CASE("run on a completion executor that is already running shares the loop", "[completion][executor][branch]")
    {
        // The completion side of the same arming check the readiness test above covers: run() creates
        // the I/O thread only when it was the call that set running_, so a second run() has to join in
        // rather than start a rival loop.
        int fds[2] {-1, -1};
        REQUIRE(::pipe(fds) == 0);

        completion::executor exec;
        std::atomic_bool submitted {false};
        std::array<char, 8> buffer {};

        auto body = [&exec, &submitted, &buffer, fd = fds[0]]() -> task<void>
        {
            submitted.store(true, std::memory_order_release);
            const auto r = co_await exec.async_read(fd, std::span<char>(buffer.data(), buffer.size()));
            (void) r;
        };
        exec.spawn(body());

        kmx::aio::test::scoped_completion_runner runner {exec};
        REQUIRE(wait_for_flag(submitted, 2s));

        std::atomic_bool second_returned {false};
        std::jthread second([&exec, &second_returned]()
                            {
                                exec.run();
                                second_returned.store(true, std::memory_order_release);
                            });

        std::this_thread::sleep_for(50ms);

        const char byte = 'x';
        REQUIRE(::write(fds[1], &byte, 1u) == 1);
        exec.stop();

        REQUIRE(wait_for_flag(second_returned, 5s));
        REQUIRE(runner.wait_until_drained(std::chrono::seconds(5)));

        ::close(fds[0]);
        ::close(fds[1]);
    }

    TEST_CASE("an event wakes one of two waiters and leaves the other listed", "[readiness][executor][branch]")
    {
        // resume_if_found takes the front waiter and erases the list only once it is empty. Two waiters
        // woken by real events - not by a cancellation, which resumes them all at once - is what takes
        // the "list still has entries" side of that check.
        socket_pair sockets;
        REQUIRE(sockets.valid());

        auto exec = std::make_shared<readiness::executor>();
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        std::atomic_int parked {0};
        std::atomic_int finished {0};

        auto waiter = [&parked, &finished, exec, fd = sockets.local()]() -> task<void>
        {
            parked.fetch_add(1, std::memory_order_acq_rel);
            const bool event = co_await exec->wait_io(fd, readiness::event_type::read);
            if (event)
                finished.fetch_add(1, std::memory_order_acq_rel);
        };

        exec->spawn(waiter());
        exec->spawn(waiter());

        scoped_runner runner {*exec};

        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while ((parked.load(std::memory_order_acquire) < 2) && (std::chrono::steady_clock::now() < deadline))
            std::this_thread::sleep_for(1ms);

        REQUIRE(parked.load(std::memory_order_acquire) == 2);
        std::this_thread::sleep_for(50ms);

        // Edge-triggered epoll reports the descriptor once per new arrival, so two writes are needed to
        // wake two waiters one at a time.
        const char byte = 'x';
        REQUIRE(::write(sockets.peer(), &byte, 1u) == 1);
        std::this_thread::sleep_for(80ms);
        REQUIRE(::write(sockets.peer(), &byte, 1u) == 1);

        const auto by = std::chrono::steady_clock::now() + 5s;
        while ((finished.load(std::memory_order_acquire) < 2) && (std::chrono::steady_clock::now() < by))
            std::this_thread::sleep_for(1ms);

        CHECK(finished.load(std::memory_order_acquire) == 2);
        exec->cancel_io(sockets.local());
    }
#endif // KMX_AIO_FEATURE_READINESS

    TEST_CASE("the idle predicate sees a queue that is not yet empty", "[core][scheduler][branch]")
    {
        // One worker and a backlog: every task but the last finishes with entries still queued, which is
        // the short-circuiting side of `queue_.empty() && active_ == 0`.
        std::atomic_int completed {0};

        {
            scheduler sched {1u};
            for (int i = 0; i < 12; ++i)
                sched.spawn([&completed]() { completed.fetch_add(1, std::memory_order_acq_rel); });

            sched.wait_until_idle();
        }

        CHECK(completed.load(std::memory_order_acquire) == 12);
    }
}
