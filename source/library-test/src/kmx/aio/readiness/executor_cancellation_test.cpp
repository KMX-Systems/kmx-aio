/// @file aio/readiness/executor_cancellation_test.cpp
/// @brief Regression tests for: a wait_io() suspension must always be resumed.
///
/// Bug reproduced: executor::unregister_fd() erased the subscriptions for a descriptor without
/// resuming the coroutines waiting in them. Once the descriptor left epoll no event could ever arrive
/// for it, so those coroutines stayed suspended for good: their frames were never destroyed and the
/// tasks holding them never completed. run(), which returns when its work drains, then waited on work
/// that could no longer make progress - an idle process hung with nothing left to wake it.
///
/// The same gap had no cure at all for a descriptor that stays registered but will never see another
/// event, such as a listener whose server is shutting down; cancel_io() is the way to end those waits.
///
/// Tests:
///   1. unregister_fd() resumes a parked wait, and reports it as cancelled rather than as an event.
///   2. cancel_io() does the same for a descriptor that stays registered.
///   3. A cancel that lands before the wait subscribes is not lost.
///   4. register_fd() re-arms a cancelled descriptor, so a later wait is woken by a real event.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>

#include <sys/socket.h>
#include <unistd.h>

#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/task.hpp>
#include <kmx/aio/test/executor_runner.hpp>
#include <kmx/aio/test/fd_pair.hpp>
#include <kmx/aio/test/outcome.hpp>

namespace kmx::aio::test::readiness::executor_cancellation_test
{
    using namespace kmx::aio::readiness;

    using namespace std::literals::chrono_literals;
    using kmx::aio::test::scoped_runner;
    using kmx::aio::test::wait_for_flag;

    // 1. unregister_fd() must resume what is waiting on the descriptor
    TEST_CASE("readiness executor: unregister_fd resumes a parked wait", "[readiness][executor][cancellation]")
    {
        socket_pair sockets;
        REQUIRE(sockets.valid());

        auto exec = std::make_shared<executor>();
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        wait_outcome outcome;
        auto body = [&outcome, exec, fd = sockets.local()]() -> task<void>
        {
            outcome.parked.store(true, std::memory_order_release);
            const bool fired = co_await exec->wait_io(fd, event_type::read);
            outcome.fired.store(fired, std::memory_order_release);
            outcome.completed.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(outcome.parked, 2s));

        // Nothing is ever written to the peer, so only the unregistration below can end this wait.
        std::this_thread::sleep_for(50ms);
        exec->unregister_fd(sockets.local());

        REQUIRE(runner.wait_until_drained(5s));
        CHECK(outcome.completed.load(std::memory_order_acquire));
        CHECK_FALSE(outcome.fired.load(std::memory_order_acquire));
    }

    // 2. cancel_io() must do the same while the descriptor stays registered
    TEST_CASE("readiness executor: cancel_io resumes a parked wait", "[readiness][executor][cancellation]")
    {
        socket_pair sockets;
        REQUIRE(sockets.valid());

        auto exec = std::make_shared<executor>();
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        wait_outcome outcome;
        auto body = [&outcome, exec, fd = sockets.local()]() -> task<void>
        {
            outcome.parked.store(true, std::memory_order_release);
            const bool fired = co_await exec->wait_io(fd, event_type::read);
            outcome.fired.store(fired, std::memory_order_release);
            outcome.completed.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(outcome.parked, 2s));
        std::this_thread::sleep_for(50ms);
        exec->cancel_io(sockets.local());

        REQUIRE(runner.wait_until_drained(5s));
        CHECK(outcome.completed.load(std::memory_order_acquire));
        CHECK_FALSE(outcome.fired.load(std::memory_order_acquire));
    }

    // 3. A cancel arriving before the wait must not be lost
    TEST_CASE("readiness executor: a cancel before the wait is not lost", "[readiness][executor][cancellation]")
    {
        socket_pair sockets;
        REQUIRE(sockets.valid());

        auto exec = std::make_shared<executor>();
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        // The cancellation happens first. A subscription that only consulted the waiter list would find
        // nothing to cancel and would then park behind it - the lost wake-up this ordering exists to
        // catch, and the reason the decision is made inside subscribe() under the same lock.
        exec->cancel_io(sockets.local());

        wait_outcome outcome;
        auto body = [&outcome, exec, fd = sockets.local()]() -> task<void>
        {
            outcome.parked.store(true, std::memory_order_release);
            const bool fired = co_await exec->wait_io(fd, event_type::read);
            outcome.fired.store(fired, std::memory_order_release);
            outcome.completed.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        // Asserted on the task rather than on run() draining: this wait never suspends, so the task can
        // finish before run() is even entered - and run() samples the outstanding work on entry, so a
        // drain that already happened is one it will wait for forever. That is a property of run(),
        // not of cancellation, and it is not what this test is about.
        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(outcome.completed, 5s));
        CHECK_FALSE(outcome.fired.load(std::memory_order_acquire));
    }

    // 4. register_fd() re-arms a descriptor that was cancelled
    TEST_CASE("readiness executor: register_fd re-arms a cancelled descriptor", "[readiness][executor][cancellation]")
    {
        socket_pair sockets;
        REQUIRE(sockets.valid());

        auto exec = std::make_shared<executor>();
        REQUIRE(exec->register_fd(sockets.local()).has_value());
        exec->cancel_io(sockets.local());
        exec->unregister_fd(sockets.local());

        // The kernel hands out the lowest free descriptor number, so a cancelled one comes back as an
        // unrelated socket soon enough - which is this sequence, minus the close. Registering it again
        // has to clear the mark, or every wait on the new socket would report a cancellation left
        // behind by the old one.
        REQUIRE(exec->register_fd(sockets.local()).has_value());

        wait_outcome outcome;
        auto body = [&outcome, exec, fd = sockets.local()]() -> task<void>
        {
            outcome.parked.store(true, std::memory_order_release);
            const bool fired = co_await exec->wait_io(fd, event_type::read);
            outcome.fired.store(fired, std::memory_order_release);
            outcome.completed.store(true, std::memory_order_release);
        };
        exec->spawn(body());

        scoped_runner runner {*exec};
        REQUIRE(wait_for_flag(outcome.parked, 2s));
        std::this_thread::sleep_for(50ms);

        const char byte = 'x';
        REQUIRE(::write(sockets.peer(), &byte, 1u) == 1);

        REQUIRE(runner.wait_until_drained(5s));
        CHECK(outcome.completed.load(std::memory_order_acquire));
        CHECK(outcome.fired.load(std::memory_order_acquire));
    }

} // namespace kmx::aio::test::readiness::executor_cancellation_test
