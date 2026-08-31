/// @file aio/task_internals_test.cpp
/// @brief Tests for the task promise machinery: exception capture per result type, stop-token
///        inheritance, and the allocation path a coroutine frame takes when the slab is exhausted.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// task<T> is a template, so each result type is a separate promise instantiation with its own
/// unhandled_exception and return_value. The library's own tasks are mostly
/// task_returning_expected_size_t and its siblings, and nothing in the suite makes
/// one of those throw - the exception path exists once per instantiation and was taken in none of them.
#include <catch2/catch_test_macros.hpp>

#include <coroutine>
#include <expected>
#include <stdexcept>
#include <stop_token>
#include <system_error>
#include <utility>
#include <vector>

#include <kmx/aio/allocator/slab.hpp>
#include <kmx/aio/allocator/statistics.hpp>
#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::test::task_internals_test
{
    namespace detail
    {
        struct task_error: std::runtime_error
        {
            task_error(): std::runtime_error("thrown from a task body") {}
        };

        // One throwing coroutine per result type the library instantiates, so the promise under test is
        // that instantiation's own rather than a shared one.
        task_returning_expected_size_t throwing_size_task()
        {
            throw task_error {};
            co_return expected_size_t {0u};
        }

        task<expected_int_t> throwing_int_result_task()
        {
            throw task_error {};
            co_return expected_int_t {0};
        }

        task_returning_expected_void_t throwing_void_result_task()
        {
            throw task_error {};
            co_return expected_void_t {};
        }

        task<int> int_task(const int value)
        {
            co_return value;
        }

        task<int> awaits_int_task(const int value)
        {
            const int inner = co_await int_task(value);
            co_return inner + 1;
        }

        /// @brief Runs @p body to completion on its own completion executor.
        template <typename body_t>
        void run_one(body_t& body)
        {
            completion::executor exec;
            exec.spawn(body(exec));
            exec.run();
        }
    } // namespace detail

    TEST_CASE("a throwing expected<size_t> task reports through its own promise", "[core][task][exception]")
    {
        bool caught = false;
        auto body = [&caught](completion::executor& exec) -> task<void>
        {
            try
            {
                const auto result = co_await detail::throwing_size_task();
                (void) result;
            }
            catch (const detail::task_error&)
            {
                caught = true;
            }

            exec.stop();
        };
        detail::run_one(body);
        CHECK(caught);
    }

    TEST_CASE("a throwing expected<int> task reports through its own promise", "[core][task][exception]")
    {
        bool caught = false;
        auto body = [&caught](completion::executor& exec) -> task<void>
        {
            try
            {
                const auto result = co_await detail::throwing_int_result_task();
                (void) result;
            }
            catch (const detail::task_error&)
            {
                caught = true;
            }

            exec.stop();
        };
        detail::run_one(body);
        CHECK(caught);
    }

    TEST_CASE("a throwing expected<void> task reports through its own promise", "[core][task][exception]")
    {
        bool caught = false;
        auto body = [&caught](completion::executor& exec) -> task<void>
        {
            try
            {
                static_cast<void>(co_await detail::throwing_void_result_task());
            }
            catch (const detail::task_error&)
            {
                caught = true;
            }

            exec.stop();
        };
        detail::run_one(body);
        CHECK(caught);
    }

    TEST_CASE("a task<int> returns its value to an awaiting task", "[core][task]")
    {
        // return_value and await_suspend are per-instantiation too: a task<int> awaited from a task<int>
        // promise is a different await_suspend than the task<void> case the rest of the suite uses.
        int observed = 0;
        auto body = [&observed](completion::executor& exec) -> task<void>
        {
            observed = co_await detail::awaits_int_task(41);
            exec.stop();
        };
        detail::run_one(body);
        CHECK(observed == 42);
    }

    TEST_CASE("a task can await an ordinary awaitable", "[core][task][await_transform]")
    {
        // await_transform forwards anything that is not the stop-token tag; nothing else in the suite
        // hands a task an awaitable that is not another task.
        bool resumed = false;
        auto body = [&resumed](completion::executor& exec) -> task<void>
        {
            co_await std::suspend_never {};
            resumed = true;
            exec.stop();
        };
        detail::run_one(body);
        CHECK(resumed);
    }

    TEST_CASE("a sub-task inherits the stop token of the task awaiting it", "[core][task][stop_token]")
    {
        // await_suspend copies the awaiting coroutine's token into a sub-task that has none of its own,
        // which is what makes cancelling an outer task reach everything it is waiting on.
        std::stop_source source;
        bool token_possible = false;
        bool token_requested = false;

        auto inner = [&token_possible, &token_requested]() -> task<void>
        {
            const auto token = co_await get_stop_token;
            token_possible = token.stop_possible();
            token_requested = token.stop_requested();
        };

        completion::executor exec;
        auto body = [&inner, &exec]() -> task<void>
        {
            co_await inner();
            exec.stop();
        };

        source.request_stop();
        exec.spawn(std::move(body()).with_stop_token(source.get_token()));
        exec.run();

        CHECK(token_possible);
        CHECK(token_requested);
    }

    TEST_CASE("a sub-task keeps a stop token of its own", "[core][task][stop_token]")
    {
        // The other side of that branch: a task already given a token is not handed the parent's.
        std::stop_source outer;
        std::stop_source narrow;
        bool requested = false;

        auto inner = [&requested]() -> task<void>
        {
            const auto token = co_await get_stop_token;
            requested = token.stop_requested();
        };

        completion::executor exec;
        auto body = [&inner, &exec, &narrow]() -> task<void>
        {
            co_await std::move(inner()).with_stop_token(narrow.get_token());
            exec.stop();
        };

        narrow.request_stop(); // the inner token is signalled, the outer one is not
        exec.spawn(std::move(body()).with_stop_token(outer.get_token()));
        exec.run();

        CHECK(requested);
    }

    TEST_CASE("a coroutine frame falls back to the heap when the slab is exhausted", "[core][task][allocator]")
    {
        // promise_base::operator new prefers the thread-local slab and drops to ::operator new when it
        // cannot serve the frame. The existing slab test covers a frame too large for a slot; this is
        // the other arm - a frame that fits, arriving when every slot is already handed out.
        allocator::slab slab {1024u, 2u};
        set_thread_allocator(&slab);

        const auto before = get_allocator_statistics().heap_allocations.load(std::memory_order_relaxed);

        // Many more live coroutines than the slab has slots, so allocation has to spill over.
        std::vector<task<int>> tasks;
        tasks.reserve(32u);
        for (int i = 0; i < 32; ++i)
            tasks.push_back(detail::int_task(i));

        const auto after = get_allocator_statistics().heap_allocations.load(std::memory_order_relaxed);
        const auto slab_used = get_allocator_statistics().slab_allocations.load(std::memory_order_relaxed);

        // Order matters, and getting it wrong aborts rather than leaks: operator delete decides where a
        // frame came from by asking the *current* thread allocator whether it owns the pointer. Clearing
        // the allocator first would send the slab-backed frames to ::operator delete, which is a free()
        // of a pointer it never handed out.
        tasks.clear();
        set_thread_allocator(nullptr);

        CHECK(slab_used > 0u); // the slab served what it could
        CHECK(after > before); // and the rest spilled to the heap
    }
} // namespace kmx::aio::test::task_internals_test
