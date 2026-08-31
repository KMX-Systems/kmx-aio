/// @file aio/core_internals_test.cpp
/// @brief Unit tests for the core pieces every executor is built on: the slab allocator's fallbacks,
///        the logger, task exception propagation, and the channel edges the backpressure suite leaves.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <vector>

#include <kmx/aio/allocator/slab.hpp>
#include <kmx/aio/channel.hpp>
#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/task.hpp>
#include <kmx/logger.hpp>

namespace kmx::aio::test::core_internals_test
{
    // allocator::slab - the paths a coroutine frame takes when the slab cannot serve it
    TEST_CASE("allocator::slab hands out every slot then reports exhaustion", "[core][allocator][slab]")
    {
        // The nullptr return is not a failure: it is the signal the coroutine allocator uses to fall
        // back to the heap, so it has to be reached rather than assumed.
        allocator::slab slab {64u, 4u};
        std::vector<void*> slots;

        for (std::size_t i = 0u; i < slab.slot_count(); ++i)
        {
            void* const slot = slab.allocate();
            REQUIRE(slot != nullptr);
            slots.push_back(slot);
        }

        CHECK(slab.available() == 0u);
        CHECK(slab.allocate() == nullptr);

        for (void* const slot: slots)
            slab.deallocate(slot);

        CHECK(slab.allocated() == 0u);
        CHECK(slab.available() == slab.slot_count());
    }

    TEST_CASE("allocator::slab ignores a null deallocation", "[core][allocator][slab]")
    {
        allocator::slab slab {64u, 4u};
        void* const slot = slab.allocate();
        REQUIRE(slot != nullptr);
        REQUIRE(slab.allocated() == 1u);

        // A null pointer must not decrement the count: the fallback path frees heap frames through the
        // same call, and letting those through would drive allocated_ below zero.
        slab.deallocate(nullptr);
        CHECK(slab.allocated() == 1u);

        slab.deallocate(slot);
        CHECK(slab.allocated() == 0u);
    }

    TEST_CASE("allocator::slab disclaims a null pointer", "[core][allocator][slab]")
    {
        allocator::slab slab {64u, 4u};
        CHECK_FALSE(slab.owns(nullptr));
    }

    TEST_CASE("allocator::slab recognises only its own storage", "[core][allocator][slab]")
    {
        allocator::slab slab {64u, 4u};
        allocator::slab other {64u, 4u};

        void* const mine = slab.allocate();
        void* const theirs = other.allocate();
        REQUIRE(mine != nullptr);
        REQUIRE(theirs != nullptr);

        CHECK(slab.owns(mine));
        CHECK_FALSE(slab.owns(theirs));
        CHECK(other.owns(theirs));

        int on_the_stack = 0;
        CHECK_FALSE(slab.owns(&on_the_stack));
    }

    TEST_CASE("allocator::slab reports its geometry", "[core][allocator][slab]")
    {
        allocator::slab slab {128u, 8u};
        CHECK(slab.slot_size() >= 128u);
        CHECK(slab.slot_count() == 8u);
        CHECK(slab.allocated() == 0u);
        CHECK(slab.available() == 8u);
    }

    // logger
    TEST_CASE("the logger emits at every level", "[core][logger]")
    {
        // Errors go to stderr and everything else to stdout; both branches format and flush, and the
        // whole call is noexcept, so a broken format must not escape into the caller.
        logger::log(logger::level::error, std::source_location::current(), "error at level {}", 1);
        logger::log(logger::level::warn, std::source_location::current(), "warning at level {}", 2);
        logger::log(logger::level::info, std::source_location::current(), "info at level {}", 3);
        logger::log(logger::level::debug, std::source_location::current(), "debug at level {}", 4);
        SUCCEED("every level formatted and flushed without throwing");
    }

    TEST_CASE("the logger accepts a message with no arguments", "[core][logger]")
    {
        logger::log(logger::level::info, std::source_location::current(), "no arguments here");
        logger::log(logger::level::error, std::source_location::current(), "no arguments here either");
        SUCCEED("argument-free messages format");
    }

    // task - exception propagation
    namespace detail
    {
        struct test_error: std::runtime_error
        {
            test_error(): std::runtime_error("thrown from a task") {}
        };

        task<int> throwing_task()
        {
            throw test_error {};
            co_return 0;
        }

        task<void> throwing_void_task()
        {
            throw test_error {};
            co_return;
        }

        task<int> value_task(const int value)
        {
            co_return value;
        }
    } // namespace detail

    TEST_CASE("an exception thrown in a task body reaches the awaiting coroutine", "[core][task][exception]")
    {
        // task is lazy and awaitable-only, so the exception has to be observed the way production code
        // observes it: the body's throw is caught by the promise's unhandled_exception, parked in the
        // promise, and rethrown out of await_resume when the awaiting coroutine resumes.
        completion::executor exec;
        bool caught = false;
        bool ran = false;

        auto body = [&exec, &caught, &ran]() -> task<void>
        {
            try
            {
                const int value = co_await detail::throwing_task();
                (void) value;
            }
            catch (const detail::test_error&)
            {
                caught = true;
            }

            ran = true;
            exec.stop();
        };
        exec.spawn(body());
        exec.run();

        CHECK(ran);
        CHECK(caught);
    }

    TEST_CASE("an exception thrown in a void task reaches the awaiting coroutine", "[core][task][exception]")
    {
        completion::executor exec;
        bool caught = false;

        auto body = [&exec, &caught]() -> task<void>
        {
            try
            {
                co_await detail::throwing_void_task();
            }
            catch (const detail::test_error&)
            {
                caught = true;
            }

            exec.stop();
        };
        exec.spawn(body());
        exec.run();

        CHECK(caught);
    }

    TEST_CASE("a task carries its value to the awaiting coroutine", "[core][task]")
    {
        completion::executor exec;
        int observed = 0;

        auto body = [&exec, &observed]() -> task<void>
        {
            observed = co_await detail::value_task(41);
            exec.stop();
        };
        exec.spawn(body());
        exec.run();

        CHECK(observed == 41);
    }

    // channel - the edges the backpressure suite does not reach
    TEST_CASE("try_pop on an empty channel yields nothing", "[core][channel]")
    {
        channel<int> ch {8u};
        CHECK_FALSE(ch.try_pop().has_value());

        REQUIRE(ch.try_push(7));
        const auto value = ch.try_pop();
        REQUIRE(value.has_value());
        CHECK(*value == 7);

        CHECK_FALSE(ch.try_pop().has_value());
    }

    TEST_CASE("a capacity below two is rounded up to two slots", "[core][channel][capacity]")
    {
        // next_power_of_two floors at 2: the ring keeps one slot free to tell full from empty, so a
        // single-slot ring could never hold anything.
        channel<int> ch {1u};
        CHECK(ch.capacity() >= 2u);
        CHECK(ch.try_push(1));
    }

    TEST_CASE("a zero high watermark is clamped to one", "[core][channel][backpressure]")
    {
        channel<int> ch {8u};
        ch.set_backpressure({.low_watermark = 0u, .high_watermark = 0u});

        // A high watermark of zero would throttle a channel that has never been pushed to, so it is
        // raised to one: the producer gets exactly one slot before backpressure applies.
        CHECK(ch.try_push(1));
        CHECK_FALSE(ch.can_send());
    }

    TEST_CASE("lowering the watermark below the occupancy throttles the producer", "[core][channel][backpressure]")
    {
        channel<int> ch {16u};
        for (int i = 0; i < 6; ++i)
            REQUIRE(ch.try_push(int {i}));

        REQUIRE(ch.can_send());

        // Re-configuring below what the channel already holds has to re-evaluate the flag against the
        // current occupancy rather than wait for the next push.
        ch.set_backpressure({.low_watermark = 1u, .high_watermark = 2u});
        CHECK_FALSE(ch.can_send());
        CHECK_FALSE(ch.try_push(99));
    }

    TEST_CASE("raising the watermark releases a throttled producer", "[core][channel][backpressure]")
    {
        channel<int> ch {16u};
        ch.set_backpressure({.low_watermark = 1u, .high_watermark = 2u});

        REQUIRE(ch.try_push(1));
        REQUIRE(ch.try_push(2));
        REQUIRE_FALSE(ch.can_send());

        // Clearing the throttle has to wake anyone parked in wait_until_can_send(), not just flip the
        // flag for the next caller to notice.
        ch.set_backpressure({.low_watermark = 8u, .high_watermark = 12u});
        CHECK(ch.can_send());
        CHECK(ch.try_push(3));
    }

    TEST_CASE("a high watermark above the usable capacity is clamped", "[core][channel][backpressure]")
    {
        channel<int> ch {8u};
        ch.set_backpressure({.low_watermark = 0u, .high_watermark = 1000u});

        // The ring cannot hold more than its usable capacity, so a watermark beyond that would mean a
        // throttle that never engages and a producer that fills the ring instead.
        std::size_t pushed = 0u;
        while (ch.try_push(int {static_cast<int>(pushed)}))
            ++pushed;

        CHECK(pushed == ch.capacity() - 1u);
    }
} // namespace kmx::aio::test::core_internals_test
