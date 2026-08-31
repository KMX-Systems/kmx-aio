/// @file aio/readiness/descriptor/timer_test.cpp
/// @brief Unit tests for the readiness timerfd descriptor wrapper.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <unistd.h>

#include <kmx/aio/error_code.hpp>
#include <kmx/aio/readiness/descriptor/timer.hpp>
#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/task.hpp>
#include <kmx/aio/test/executor_runner.hpp>

namespace kmx::aio::test::readiness::descriptor::timer_test
{
    using namespace kmx::aio::readiness;
    using namespace kmx::aio::readiness::descriptor;

    using namespace std::literals::chrono_literals;
    using kmx::aio::test::scoped_runner;
    using kmx::aio::test::wait_for_flag;

    namespace detail
    {
        [[nodiscard]] ::itimerspec after(const std::chrono::nanoseconds delay) noexcept
        {
            ::itimerspec spec {};
            spec.it_value.tv_sec = delay.count() / 1'000'000'000;
            spec.it_value.tv_nsec = delay.count() % 1'000'000'000;
            return spec;
        }

        /// @brief What one timer::wait() reported, as seen from the test thread.
        struct wait_outcome
        {
            std::atomic_bool completed {false};
            std::atomic_bool ok {false};
            std::atomic_uint64_t expirations {0u};
            std::error_code error {};
        };
    } // namespace detail

    TEST_CASE("timer::create returns a valid timerfd", "[readiness][timerfd][create]")
    {
        const auto created = timer::create();
        REQUIRE(created.has_value());
        CHECK(created->is_valid());
    }

    TEST_CASE("timer::create accepts an explicit clock and flags", "[readiness][timerfd][create]")
    {
        const auto created = timer::create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
        REQUIRE(created.has_value());
        CHECK(created->is_valid());
    }

    TEST_CASE("timer::create rejects an unknown clock", "[readiness][timerfd][create][error]")
    {
        const auto created = timer::create(0x7fffffff, 0);
        REQUIRE_FALSE(created.has_value());
        CHECK(created.error() == std::errc::invalid_argument);
    }

    TEST_CASE("timer::create rejects an unknown flag", "[readiness][timerfd][create][error]")
    {
        const auto created = timer::create(CLOCK_MONOTONIC, 0x7fffffff);
        REQUIRE_FALSE(created.has_value());
        CHECK(created.error() == std::errc::invalid_argument);
    }

    TEST_CASE("set_time refuses an empty wrapper", "[readiness][timerfd][set_time][error]")
    {
        timer tmr {};
        REQUIRE_FALSE(tmr.is_valid());

        const auto spec = detail::after(10ms);
        const auto result = tmr.set_time(0, spec);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == std::errc::bad_file_descriptor);
    }

    TEST_CASE("set_time arms the timer", "[readiness][timerfd][set_time]")
    {
        auto tmr = timer::create();
        REQUIRE(tmr.has_value());

        const auto spec = detail::after(50ms);
        CHECK(tmr->set_time(0, spec).has_value());
    }

    TEST_CASE("set_time reports the previous setting", "[readiness][timerfd][set_time]")
    {
        auto tmr = timer::create();
        REQUIRE(tmr.has_value());

        const auto first = detail::after(10s);
        REQUIRE(tmr->set_time(0, first).has_value());

        ::itimerspec previous {};
        const auto second = detail::after(20ms);
        REQUIRE(tmr->set_time(0, second, &previous).has_value());

        // The old value is what remained of the 10s arming, so it must still be counting down.
        CHECK(previous.it_value.tv_sec > 0);
    }

    TEST_CASE("set_time disarms with a zero spec", "[readiness][timerfd][set_time]")
    {
        auto tmr = timer::create();
        REQUIRE(tmr.has_value());
        REQUIRE(tmr->set_time(0, detail::after(10s)).has_value());

        ::itimerspec previous {};
        const ::itimerspec disarm {};
        REQUIRE(tmr->set_time(0, disarm, &previous).has_value());
        CHECK(previous.it_value.tv_sec > 0);

        ::itimerspec now_disarmed {};
        const ::itimerspec again {};
        REQUIRE(tmr->set_time(0, again, &now_disarmed).has_value());
        CHECK(now_disarmed.it_value.tv_sec == 0);
        CHECK(now_disarmed.it_value.tv_nsec == 0);
    }

    TEST_CASE("set_time rejects an out-of-range spec", "[readiness][timerfd][set_time][error]")
    {
        auto tmr = timer::create();
        REQUIRE(tmr.has_value());

        // tv_nsec must be below 1e9; timerfd_settime rejects anything else with EINVAL.
        ::itimerspec spec {};
        spec.it_value.tv_nsec = 2'000'000'000;

        const auto result = tmr->set_time(0, spec);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == std::errc::invalid_argument);
    }

    TEST_CASE("timer::wait resumes once the timer expires", "[readiness][timerfd][wait]")
    {
        // The interesting path: the first read returns EAGAIN on a not-yet-expired non-blocking timerfd,
        // the coroutine parks on the executor, and the expiry wakes it for a second read that succeeds.
        auto tmr = timer::create();
        REQUIRE(tmr.has_value());
        REQUIRE(tmr->set_time(0, detail::after(30ms)).has_value());

        auto exec = std::make_shared<executor>();
        REQUIRE(exec->register_fd(tmr->get()).has_value());

        detail::wait_outcome outcome;
        auto body = [&outcome, &timer_ref = *tmr, exec]() -> task<void>
        {
            const auto result = co_await timer_ref.wait(*exec);
            if (result)
            {
                outcome.ok.store(true, std::memory_order_release);
                outcome.expirations.store(*result, std::memory_order_release);
            }
            else
            {
                outcome.error = result.error();
            }

            outcome.completed.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(runner.wait_until_drained(5s));
        CHECK(outcome.completed.load(std::memory_order_acquire));
        CHECK(outcome.ok.load(std::memory_order_acquire));
        CHECK(outcome.expirations.load(std::memory_order_acquire) == 1u);
    }

    TEST_CASE("timer::wait returns immediately for an already-expired timer", "[readiness][timerfd][wait]")
    {
        // The first read succeeds outright, so the wait never parks - the loop's fast path.
        auto tmr = timer::create();
        REQUIRE(tmr.has_value());
        REQUIRE(tmr->set_time(0, detail::after(1ms)).has_value());
        std::this_thread::sleep_for(30ms);

        auto exec = std::make_shared<executor>();
        REQUIRE(exec->register_fd(tmr->get()).has_value());

        detail::wait_outcome outcome;
        auto body = [&outcome, &timer_ref = *tmr, exec]() -> task<void>
        {
            const auto result = co_await timer_ref.wait(*exec);
            if (result)
            {
                outcome.ok.store(true, std::memory_order_release);
                outcome.expirations.store(*result, std::memory_order_release);
            }

            outcome.completed.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        // This wait never parks, so no epoll event follows it and run() gets no occasion to re-check
        // whether its work has drained. The task's own flag is the signal here; scoped_runner stops the
        // loop on the way out.
        REQUIRE(wait_for_flag(outcome.completed, 5s));
        CHECK(outcome.ok.load(std::memory_order_acquire));
        CHECK(outcome.expirations.load(std::memory_order_acquire) >= 1u);
    }

    TEST_CASE("timer::wait counts the expirations of a periodic timer", "[readiness][timerfd][wait]")
    {
        auto tmr = timer::create();
        REQUIRE(tmr.has_value());

        ::itimerspec spec = detail::after(5ms);
        spec.it_interval.tv_nsec = 5'000'000; // 5ms period
        REQUIRE(tmr->set_time(0, spec).has_value());
        std::this_thread::sleep_for(60ms);

        auto exec = std::make_shared<executor>();
        REQUIRE(exec->register_fd(tmr->get()).has_value());

        detail::wait_outcome outcome;
        auto body = [&outcome, &timer_ref = *tmr, exec]() -> task<void>
        {
            const auto result = co_await timer_ref.wait(*exec);
            if (result)
            {
                outcome.ok.store(true, std::memory_order_release);
                outcome.expirations.store(*result, std::memory_order_release);
            }

            outcome.completed.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        // As above: the read succeeds without parking, so the task's flag is the signal, not run()
        // returning. Waiting on the drain here would pass or fail on whether the next period happened to
        // wake the loop first.
        REQUIRE(wait_for_flag(outcome.completed, 5s));
        CHECK(outcome.ok.load(std::memory_order_acquire));
        // Several periods elapsed before the read, and timerfd reports them as one accumulated count.
        CHECK(outcome.expirations.load(std::memory_order_acquire) > 1u);
    }

    TEST_CASE("timer::wait reports a cancelled wait", "[readiness][timerfd][wait][cancellation]")
    {
        // Drives the arm where wait_io() reports a cancel rather than an event: the timer is armed far
        // enough out that only the cancel can end the wait.
        auto tmr = timer::create();
        REQUIRE(tmr.has_value());
        REQUIRE(tmr->set_time(0, detail::after(10s)).has_value());

        auto exec = std::make_shared<executor>();
        REQUIRE(exec->register_fd(tmr->get()).has_value());

        std::atomic_bool parked {false};
        detail::wait_outcome outcome;
        auto body = [&outcome, &parked, &timer_ref = *tmr, exec]() -> task<void>
        {
            parked.store(true, std::memory_order_release);
            const auto result = co_await timer_ref.wait(*exec);
            if (!result)
                outcome.error = result.error();
            else
                outcome.ok.store(true, std::memory_order_release);

            outcome.completed.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(parked, 2s));
        std::this_thread::sleep_for(50ms);
        exec->cancel_io(tmr->get());

        REQUIRE(runner.wait_until_drained(5s));
        CHECK(outcome.completed.load(std::memory_order_acquire));
        CHECK_FALSE(outcome.ok.load(std::memory_order_acquire));
        CHECK(outcome.error == aio::to_std_error_code(aio::error_code::operation_cancelled));
    }

    TEST_CASE("timer::wait reports a read failure", "[readiness][timerfd][wait][error]")
    {
        // A timer wrapping a descriptor that is not a timerfd: the read fails with something other than
        // EAGAIN, which is the wait loop's error return.
        int fds[2] {-1, -1};
        REQUIRE(::pipe(fds) == 0);

        // The write end is not readable, so ::read fails with EBADF rather than blocking.
        timer tmr {fds[1]};

        auto exec = std::make_shared<executor>();
        detail::wait_outcome outcome;
        auto body = [&outcome, &tmr, exec]() -> task<void>
        {
            const auto result = co_await tmr.wait(*exec);
            if (!result)
                outcome.error = result.error();
            else
                outcome.ok.store(true, std::memory_order_release);

            outcome.completed.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        // This wait never parks, so no epoll event follows it and run() gets no occasion to re-check
        // whether its work has drained. The task's own flag is the signal here; scoped_runner stops the
        // loop on the way out.
        REQUIRE(wait_for_flag(outcome.completed, 5s));
        CHECK_FALSE(outcome.ok.load(std::memory_order_acquire));
        CHECK(outcome.error == std::errc::bad_file_descriptor);

        ::close(fds[0]);
    }
} // namespace kmx::aio::test::readiness::descriptor::timer_test
