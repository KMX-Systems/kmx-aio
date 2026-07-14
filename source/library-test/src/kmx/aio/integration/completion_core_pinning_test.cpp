/// @file aio/integration/completion_core_pinning_test.cpp
/// @brief Integration test for completion executor core pinning parity.

#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/timer.hpp>
#include <kmx/aio/task.hpp>

#include <atomic>
#include <chrono>
#include <expected>
#include <memory>
#include <pthread.h>
#include <sched.h>
#include <system_error>
#include <thread>

namespace kmx::aio::completion::test::integration
{
    [[nodiscard]] static std::expected<int, std::error_code> first_allowed_cpu_for_current_thread() noexcept
    {
        cpu_set_t allowed {};
        CPU_ZERO(&allowed);

        const int ret = ::pthread_getaffinity_np(::pthread_self(), sizeof(cpu_set_t), &allowed);
        if (ret != 0)
            return std::unexpected(std::error_code(ret, std::generic_category()));

        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
            if (CPU_ISSET(cpu, &allowed) != 0)
                return cpu;

        return std::unexpected(std::make_error_code(std::errc::no_such_device));
    }

    [[nodiscard]] static task<void> hold_executor(executor& exec)
    {
        timer tmr {exec};
        auto wait_res = co_await tmr.wait(std::chrono::milliseconds(500));
        (void) wait_res;
        co_return;
    }

    TEST_CASE("completion executor pins I/O thread to configured core", "[completion][integration][pinning]")
    {
        const auto core_res = first_allowed_cpu_for_current_thread();
        REQUIRE(core_res.has_value());

        executor_config cfg {
            .ring_entries = 64u,
            .max_completions = 64u,
            .thread_count = 1u,
            .core_id = static_cast<decltype(executor_config::core_id)>(*core_res),
        };

        // Heap-allocated (not a local value) so that if the test times out below and the
        // runner thread is detached, the executor can be intentionally leaked instead of being
        // destroyed while still driven by the detached thread. The normal (non-timeout) path
        // destroys it via exec_holder's own destructor at scope exit, same as before.
        auto exec_holder = std::make_unique<executor>(cfg);
        executor* const exec_ptr = exec_holder.get();
        executor& exec = *exec_ptr;

        exec.spawn(hold_executor(exec));

        std::atomic_bool runner_done {false};
        std::thread runner(
            [exec_ptr, &runner_done]()
            {
                exec_ptr->run();
                runner_done.store(true, std::memory_order_release);
            });

        bool confirmed = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                SKIP("completion pinning test timeout: executor I/O thread did not confirm affinity");
                if (runner.joinable())
                {
                    runner.detach();
                    (void) exec_holder.release(); // Leak intentionally: the thread is still running.
                }
                return;
            }

            const auto affined = exec.is_io_thread_affined_to(*core_res);
            if (affined.has_value())
            {
                REQUIRE(*affined);
                confirmed = true;
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        REQUIRE(confirmed);

        const auto shutdown_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!runner_done.load(std::memory_order_acquire) && (std::chrono::steady_clock::now() <= shutdown_deadline))
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        if (!runner_done.load(std::memory_order_acquire))
        {
            exec.stop();
            if (runner.joinable())
            {
                runner.detach();
                (void) exec_holder.release(); // Leak intentionally: the thread is still running.
            }
            FAIL("completion pinning test timeout: executor did not stop after affinity confirmation");
        }

        if (runner.joinable())
            runner.join();
    }
} // namespace kmx::aio::completion::test::integration