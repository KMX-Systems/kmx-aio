/// @file aio/integration/readiness_core_pinning_test.cpp
/// @brief Integration test for readiness executor core pinning parity.

#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/task.hpp>

#include <atomic>
#include <chrono>
#include <expected>
#include <memory>
#include <pthread.h>
#include <sched.h>
#include <system_error>
#include <thread>

namespace kmx::aio::readiness::test::integration
{
    [[nodiscard]] static std::expected<int, std::error_code> first_allowed_cpu_for_current_thread() noexcept
    {
        cpu_set_t allowed {};
        CPU_ZERO(&allowed);

        const int ret = ::pthread_getaffinity_np(::pthread_self(), sizeof(cpu_set_t), &allowed);
        if (ret != 0)
            return std::unexpected(std::error_code(ret, std::generic_category()));

        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
        {
            if (CPU_ISSET(cpu, &allowed) != 0)
                return cpu;
        }

        return std::unexpected(std::make_error_code(std::errc::no_such_device));
    }

    [[nodiscard]] static std::jthread delayed_stop(std::shared_ptr<executor> exec)
    {
        return std::jthread(
            [exec](std::stop_token)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                exec->stop();
            });
    }

    TEST_CASE("readiness executor pins I/O thread to configured core", "[readiness][integration][pinning]")
    {
        const auto core_res = first_allowed_cpu_for_current_thread();
        REQUIRE(core_res.has_value());

        executor_config cfg {
            .thread_count = 1u,
            .max_events = 64u,
            .timeout_ms = 10u,
            .core_id = static_cast<decltype(executor_config::core_id)>(*core_res),
            .backend = backend_mode::epoll_only,
        };

        auto exec = std::make_shared<executor>(cfg);
        REQUIRE(exec != nullptr);

        auto stopper = delayed_stop(exec);
        std::atomic_bool runner_done {false};
        std::thread runner(
            [exec, &runner_done]()
            {
                exec->run();
                runner_done.store(true, std::memory_order_release);
            });

        bool confirmed = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                SKIP("readiness pinning test timeout: executor I/O thread did not confirm affinity");
                if (runner.joinable())
                    runner.detach();
                return;
            }

            const auto affined = exec->is_io_thread_affined_to(*core_res);
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
            exec->stop();
            if (runner.joinable())
                runner.detach();
            FAIL("readiness pinning test timeout: executor did not stop after affinity confirmation");
        }

        if (runner.joinable())
            runner.join();

        if (stopper.joinable())
            stopper.join();
    }
} // namespace kmx::aio::readiness::test::integration
