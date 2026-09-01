/// @file aio/gpu/executor_reentrancy_test.cpp
/// @brief Covers what a coroutine resumed by the GPU executor is allowed to do while it runs.
/// @details Every case here drives the executor's poll loop and has the resumed coroutine call back
///          into the executor - awaiting another event, spawning a task, or waiting on the same event
///          handle again. All three take the executor's queue mutex, so they are exactly what a
///          resumption performed while that mutex is held cannot do.
/// @note A regression shows up as a hang, not as a failed assertion: re-locking a non-recursive
///       std::mutex from the thread that already owns it is undefined behaviour, and on this platform
///       it blocks forever. script/feature/cuda/run-unit-tests.sh runs the binary under `timeout 90s`,
///       which is what turns that hang into a failing build.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/gpu/event.hpp>
#include <kmx/aio/gpu/executor.hpp>
#include <kmx/aio/gpu/stream.hpp>
#include <kmx/aio/task.hpp>

#include <coroutine>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <stop_token>
#include <utility>
#include <vector>

namespace kmx::aio::test::gpu::executor_reentrancy_test
{
    namespace detail
    {
        /// @brief A coroutine whose entire body is one caller-supplied action.
        /// @details Suspends at both ends: at the start so the test decides when it runs, and at the end
        ///          so the frame outlives its own completion and the test can destroy it deterministically
        ///          rather than racing the executor for it.
        struct probe
        {
            /// @brief Promise of @ref probe; carries no result and never resumes anyone.
            struct promise_type
            {
                /// @brief Builds the handle wrapper handed back to the factory.
                /// @return A @ref probe owning this promise's coroutine handle.
                probe get_return_object() noexcept { return probe {std::coroutine_handle<promise_type>::from_promise(*this)}; }
                /// @brief Suspends before the body runs, so the first resume() is the test's.
                /// @return An always-suspending awaiter.
                std::suspend_always initial_suspend() const noexcept { return {}; }
                /// @brief Suspends instead of destroying the frame, leaving ownership with the test.
                /// @return An always-suspending awaiter.
                std::suspend_always final_suspend() const noexcept { return {}; }
                /// @brief Completes the coroutine; the probe returns nothing.
                void return_void() const noexcept {}
                /// @brief Terminates: a probe action that throws is a broken test, not a case to handle.
                void unhandled_exception() noexcept { std::terminate(); }
            };

            /// @brief Handle of the suspended probe coroutine.
            std::coroutine_handle<promise_type> handle {};
        };

        /// @brief Creates a suspended probe that runs @p action when it is first resumed.
        /// @param action The callable to run on resumption; copied into the coroutine frame.
        /// @return The suspended probe.
        [[nodiscard]] probe make_probe(std::function<void()> action) noexcept(false)
        {
            action();
            co_return;
        }

        /// @brief Owns a probe's coroutine frame and destroys it at the end of the test.
        class probe_owner
        {
        public:
            /// @brief Adopts @p created probe's frame.
            /// @param created The probe whose frame this owner takes over.
            explicit probe_owner(probe created) noexcept: handle_(created.handle) {}

            /// @brief Destroys the owned frame.
            ~probe_owner() noexcept
            {
                if (handle_)
                    handle_.destroy();
            }

            /// @brief Non-copyable.
            probe_owner(const probe_owner&) = delete;
            /// @brief Non-copyable.
            probe_owner& operator=(const probe_owner&) = delete;

            /// @brief The type-erased handle to hand to the executor.
            /// @return The probe's coroutine handle.
            [[nodiscard]] kmx::aio::coroutine_handle_t handle() const noexcept { return handle_; }

        private:
            /// @brief The owned frame.
            std::coroutine_handle<probe::promise_type> handle_;
        };

        /// @brief A task the executor can be asked to spawn; records that it ran.
        /// @param ran Set to true when the task body executes.
        /// @return The spawnable task.
        [[nodiscard]] kmx::aio::task<void> record_task(bool* const ran) noexcept(false)
        {
            *ran = true;
            co_return;
        }

        /// @brief Records @p count events on one stream and waits for all of them to signal.
        /// @param source The stream to record on.
        /// @param count  How many events to record.
        /// @return The recorded events, every one of them already signaled.
        /// @details A signaled event is what makes poll_events() resume the coroutine registered on it,
        ///          so every case below needs its events in this state before it drives the loop.
        [[nodiscard]] std::vector<kmx::aio::gpu::event> make_signaled_events(kmx::aio::gpu::stream& source,
                                                                             const std::size_t count) noexcept(false)
        {
            std::vector<kmx::aio::gpu::event> events;
            events.reserve(count);
            for (std::size_t i {}; i < count; ++i)
                events.push_back(source.create_event());

            source.synchronize();
            return events;
        }

        /// @brief Runs the executor's loop until it has no pending task and no waiting event left.
        /// @param exec The executor to drive.
        /// @details The stop is requested up front, so the loop keeps polling only for as long as
        ///          has_pending_work() says there is something outstanding - including work that a
        ///          resumption added while the loop was running.
        void drain(kmx::aio::gpu::executor& exec) noexcept(false)
        {
            std::stop_source source;
            source.request_stop();
            exec.run(source.get_token());
        }
    } // namespace detail

    TEST_CASE("GPU executor resumption may await a second event", "[gpu][executor][poll][reentrancy]")
    {
        auto exec = std::make_shared<kmx::aio::gpu::executor>();
        kmx::aio::gpu::stream work_stream;
        auto events = detail::make_signaled_events(work_stream, 2u);

        bool second_ran {};
        const detail::probe_owner second {detail::make_probe([&second_ran] { second_ran = true; })};

        // What a coroutine awaiting two GPU events in sequence does, reduced to the one call that
        // matters: registering with the executor from inside a resumption the executor is performing.
        bool first_ran {};
        const detail::probe_owner first {detail::make_probe(
            [&]
            {
                first_ran = true;
                exec->register_waiting_coroutine(events[1u].handle(), second.handle());
            })};

        exec->register_waiting_coroutine(events[0u].handle(), first.handle());

        detail::drain(*exec);

        REQUIRE(first_ran);
        REQUIRE(second_ran);
        REQUIRE(exec->get_statistics().total_events_completed.load() == 2u);
    }

    TEST_CASE("GPU executor resumption may spawn a task", "[gpu][executor][poll][reentrancy]")
    {
        auto exec = std::make_shared<kmx::aio::gpu::executor>();
        kmx::aio::gpu::stream work_stream;
        auto events = detail::make_signaled_events(work_stream, 1u);

        bool spawned_ran {};
        bool waiter_ran {};
        const detail::probe_owner waiter {detail::make_probe(
            [&]
            {
                waiter_ran = true;
                exec->spawn(detail::record_task(&spawned_ran));
            })};

        exec->register_waiting_coroutine(events[0u].handle(), waiter.handle());

        detail::drain(*exec);

        REQUIRE(waiter_ran);
        REQUIRE(spawned_ran);
        REQUIRE(exec->get_statistics().total_tasks_completed.load() == 1u);
    }

    TEST_CASE("GPU executor resumption may wait on the same event handle again", "[gpu][executor][poll][reentrancy]")
    {
        // CUDA hands a destroyed event's address back out, so the same key genuinely does come round
        // again. The entry has to be retired before its coroutine runs for the registration made by that
        // coroutine to survive; retiring afterwards deletes the new entry along with the old one, and the
        // second waiter is then never resumed.
        auto exec = std::make_shared<kmx::aio::gpu::executor>();
        kmx::aio::gpu::stream work_stream;
        auto events = detail::make_signaled_events(work_stream, 1u);
        const auto shared_handle = events[0u].handle();

        bool second_ran {};
        const detail::probe_owner second {detail::make_probe([&second_ran] { second_ran = true; })};

        bool first_ran {};
        const detail::probe_owner first {detail::make_probe(
            [&]
            {
                first_ran = true;
                exec->register_waiting_coroutine(shared_handle, second.handle());
            })};

        exec->register_waiting_coroutine(shared_handle, first.handle());

        detail::drain(*exec);

        REQUIRE(first_ran);
        REQUIRE(second_ran);
    }

    TEST_CASE("GPU executor resumes every waiter exactly once across a rehash", "[gpu][executor][poll][reentrancy]")
    {
        // Each first-generation waiter registers a second-generation one, so the map grows while the
        // poll that is draining it is still in flight. Iterating it across those inserts is what the
        // collect-then-resume split removes: an insert that rehashes invalidates every iterator into the
        // container, including the one the loop was about to advance.
        static constexpr std::size_t generation_size = 64u;

        auto exec = std::make_shared<kmx::aio::gpu::executor>();
        kmx::aio::gpu::stream work_stream;
        auto events = detail::make_signaled_events(work_stream, generation_size * 2u);

        std::vector<int> resume_counts(generation_size * 2u, 0);
        std::vector<std::unique_ptr<detail::probe_owner>> probes;
        probes.reserve(generation_size * 2u);

        for (std::size_t i {}; i < generation_size; ++i)
        {
            const std::size_t second_index = generation_size + i;
            probes.push_back(std::make_unique<detail::probe_owner>(
                detail::make_probe([&resume_counts, second_index] { ++resume_counts[second_index]; })));
        }

        for (std::size_t i {}; i < generation_size; ++i)
        {
            const std::size_t second_index = generation_size + i;
            probes.push_back(std::make_unique<detail::probe_owner>(detail::make_probe(
                [&, i, second_index]
                {
                    ++resume_counts[i];
                    exec->register_waiting_coroutine(events[second_index].handle(), probes[i]->handle());
                })));
        }

        for (std::size_t i {}; i < generation_size; ++i)
            exec->register_waiting_coroutine(events[i].handle(), probes[generation_size + i]->handle());

        detail::drain(*exec);

        for (std::size_t i {}; i < resume_counts.size(); ++i)
            REQUIRE(resume_counts[i] == 1);

        REQUIRE(exec->get_statistics().total_events_completed.load() == generation_size * 2u);
    }

    TEST_CASE("GPU executor pending task may register an event before it is polled", "[gpu][executor][poll][reentrancy]")
    {
        // The spawn queue is drained and resumed separately from the event map, and has always resumed
        // with the lock released. Held here so that the two halves of poll_events() stay consistent with
        // each other.
        auto exec = std::make_shared<kmx::aio::gpu::executor>();
        kmx::aio::gpu::stream work_stream;
        auto events = detail::make_signaled_events(work_stream, 1u);

        bool waiter_ran {};
        const detail::probe_owner waiter {detail::make_probe([&waiter_ran] { waiter_ran = true; })};

        bool task_ran {};
        auto body = [&]() -> kmx::aio::task<void>
        {
            task_ran = true;
            exec->register_waiting_coroutine(events[0u].handle(), waiter.handle());
            co_return;
        };

        auto spawned = body();
        exec->spawn(std::move(spawned));

        detail::drain(*exec);

        REQUIRE(task_ran);
        REQUIRE(waiter_ran);
    }

} // namespace kmx::aio::test::gpu::executor_reentrancy_test
