/// @file aio/async_mutex_test.cpp
/// @brief Unit tests for the coroutine-aware mutex the TLS layer serializes its pumps with.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// Everything here runs on the test thread. A mutex that hands ownership from the releasing holder
/// straight to the next waiter is deterministic by construction: releasing it resumes exactly one
/// coroutine, that coroutine runs to its own release, and so on down the queue. So the order the
/// waiters come out in can be asserted outright rather than waited for, and none of these tests needs
/// an executor, a thread or a timeout to be meaningful.
#include <catch2/catch_test_macros.hpp>

#include <coroutine>
#include <utility>
#include <vector>

#include <kmx/aio/async_mutex.hpp>

namespace kmx::aio::test::async_mutex_test
{
    namespace detail
    {
        /// @brief A coroutine that starts on call and cleans itself up when it ends.
        /// @details task<T> is lazy - it does nothing until awaited - and awaiting it needs an
        ///          executor. These tests want the opposite: a coroutine that runs on the spot, so that
        ///          "did it suspend?" can be read off a flag straight after the call.
        struct fire_and_forget
        {
            /// @brief The promise for a coroutine that is never awaited.
            struct promise_type
            {
                /// @brief Produces the caller's handle on the coroutine.
                fire_and_forget get_return_object() const noexcept { return {}; }
                /// @brief Runs the body immediately rather than on a first await.
                std::suspend_never initial_suspend() const noexcept { return {}; }
                /// @brief Destroys the frame at the end of the body.
                std::suspend_never final_suspend() const noexcept { return {}; }
                /// @brief Completes the coroutine.
                void return_void() const noexcept {}
                /// @brief Nothing in these tests throws.
                void unhandled_exception() const noexcept {}
            };
        };
    } // namespace detail

    TEST_CASE("try_lock takes an unheld async_mutex and refuses a held one", "[core][async_mutex]")
    {
        async_mutex mutex;

        REQUIRE(mutex.try_lock());
        CHECK_FALSE(mutex.try_lock());

        mutex.unlock();
        CHECK(mutex.try_lock());
        mutex.unlock();
    }

    TEST_CASE("locking an unheld async_mutex does not suspend the caller", "[core][async_mutex]")
    {
        async_mutex mutex;
        bool reached_body = false;

        const auto enter = [&]() -> detail::fire_and_forget
        {
            const async_mutex::guard guard = co_await mutex.lock();
            reached_body = true;
        };

        enter();

        // The coroutine ran to completion inside the call: await_ready() took the mutex, so there was
        // never a suspension to be resumed from.
        CHECK(reached_body);

        // And it gave the mutex back on the way out.
        CHECK(mutex.try_lock());
        mutex.unlock();
    }

    TEST_CASE("waiters on an async_mutex are served in arrival order", "[core][async_mutex]")
    {
        async_mutex mutex;
        std::vector<int> entered;

        const auto enter = [&](const int id) -> detail::fire_and_forget
        {
            const async_mutex::guard guard = co_await mutex.lock();
            entered.push_back(id);
        };

        REQUIRE(mutex.try_lock());

        enter(1);
        enter(2);
        enter(3);

        // All three found the mutex held and queued behind the caller.
        CHECK(entered.empty());

        // Releasing hands ownership to the first waiter, which runs, releases, and hands it to the
        // second - the whole queue drains inside this one call.
        mutex.unlock();

        REQUIRE(entered.size() == 3u);
        CHECK(entered[0] == 1);
        CHECK(entered[1] == 2);
        CHECK(entered[2] == 3);

        CHECK(mutex.try_lock());
        mutex.unlock();
    }

    TEST_CASE("an async_mutex guard can be released before it goes out of scope", "[core][async_mutex]")
    {
        async_mutex mutex;
        REQUIRE(mutex.try_lock());

        async_mutex::guard guard {mutex};
        CHECK(guard.owns_lock());

        guard.release();
        CHECK_FALSE(guard.owns_lock());
        CHECK(mutex.try_lock());

        // A second release on a guard that owns nothing must not unlock somebody else's ownership.
        guard.release();
        CHECK_FALSE(mutex.try_lock());
        mutex.unlock();
    }

    TEST_CASE("moving an async_mutex guard moves the ownership with it", "[core][async_mutex][move]")
    {
        async_mutex mutex;
        REQUIRE(mutex.try_lock());

        async_mutex::guard source {mutex};
        async_mutex::guard target {std::move(source)};

        CHECK_FALSE(source.owns_lock());
        CHECK(target.owns_lock());

        // The moved-from guard releasing nothing is the point: the mutex is still held by target.
        source.release();
        CHECK_FALSE(mutex.try_lock());

        target.release();
        CHECK(mutex.try_lock());
        mutex.unlock();
    }

    TEST_CASE("an async_mutex guard releases what it holds when reassigned", "[core][async_mutex][move]")
    {
        async_mutex first;
        async_mutex second;
        REQUIRE(first.try_lock());
        REQUIRE(second.try_lock());

        async_mutex::guard guard {first};
        guard = async_mutex::guard {second};

        // The first mutex was released by the assignment, the second is what the guard holds now.
        CHECK(first.try_lock());
        first.unlock();
        CHECK_FALSE(second.try_lock());

        guard.release();
        CHECK(second.try_lock());
        second.unlock();
    }
} // namespace kmx::aio::test::async_mutex_test
