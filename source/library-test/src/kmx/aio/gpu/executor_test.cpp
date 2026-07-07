/// @file aio/gpu/executor_test.cpp
/// @brief Integration test for GPU executor, streams, and events.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/gpu/executor.hpp>

namespace
{
    /// @brief Simple GPU task that co_awaits an event.
    kmx::aio::task<int> gpu_work(std::shared_ptr<kmx::aio::gpu::executor> exec)
    {
        (void) exec;

        // Create a stream for GPU work.
        auto stream = std::make_unique<kmx::aio::gpu::stream>();

        // Submit GPU kernel (mocked as event creation).
        auto event = stream->create_event();

        // Wait for GPU work to complete (mock: immediate success).
        co_await event;

        co_return 42;
    }

} // namespace

TEST_CASE("GPU executor initializes with configuration", "[gpu][executor]")
{
    const kmx::aio::gpu::executor_config config {
        .max_events = 64u,
        .thread_count = 1u,
        .gpu_device = 0,
    };

    auto exec = std::make_shared<kmx::aio::gpu::executor>(config);
    REQUIRE(exec != nullptr);
    REQUIRE(exec->gpu_device() == 0);
}

TEST_CASE("GPU stream creation and destruction", "[gpu][stream]")
{
    auto stream = std::make_unique<kmx::aio::gpu::stream>();
    REQUIRE(stream != nullptr);
    REQUIRE(stream->handle() != nullptr);

    stream.reset(); // Destructor should clean up gracefully.
}

TEST_CASE("GPU event creation and query", "[gpu][event]")
{
    auto event = std::make_unique<kmx::aio::gpu::event>();
    REQUIRE(event != nullptr);
    REQUIRE(event->handle() != nullptr);

    // Query event status (on CUDA hardware this can be either ready or not-ready).
    REQUIRE_NOTHROW(event->is_ready());

    event.reset(); // Destructor should clean up.
}

TEST_CASE("GPU stream creates events", "[gpu][stream][event]")
{
    auto stream = std::make_unique<kmx::aio::gpu::stream>();

    // Record an event on the stream.
    auto event = stream->create_event();
    REQUIRE(event.handle() != nullptr);

    // Ensure stream work has completed before asserting event readiness.
    REQUIRE_NOTHROW(stream->synchronize());
    REQUIRE(event.is_ready());
}

TEST_CASE("GPU executor task spawning", "[gpu][executor][spawn]")
{
    auto exec = std::make_shared<kmx::aio::gpu::executor>();
    REQUIRE(exec != nullptr);

    const auto stats_before = exec->get_statistics().total_tasks_spawned.load();

    // Spawn a GPU task (doesn't actually run without calling run()).
    exec->spawn(gpu_work(exec));

    const auto stats_after = exec->get_statistics().total_tasks_spawned.load();
    REQUIRE(stats_after == stats_before + 1u);
}

TEST_CASE("GPU event is move-only", "[gpu][event][move]")
{
    auto event1 = std::make_unique<kmx::aio::gpu::event>();
    REQUIRE(event1->handle() != nullptr);

    // Move construct event2 from event1.
    kmx::aio::gpu::event event2 = std::move(*event1);
    REQUIRE(event2.handle() != nullptr);

    // event1 should be empty after move.
    REQUIRE(event1->handle() == nullptr);
}

TEST_CASE("GPU statistics reset", "[gpu][executor][statistics]")
{
    auto exec = std::make_shared<kmx::aio::gpu::executor>();

    // Spawn a few tasks to increment statistics.
    exec->spawn(gpu_work(exec));
    exec->spawn(gpu_work(exec));

    const auto& stats = exec->get_statistics();
    REQUIRE(stats.total_tasks_spawned.load() == 2u);

    // Reset statistics.
    exec->reset_statistics();

    const auto& stats_after = exec->get_statistics();
    REQUIRE(stats_after.total_tasks_spawned.load() == 0u);
}

TEST_CASE("GPU executor stops gracefully", "[gpu][executor][stop]")
{
    auto exec = std::make_shared<kmx::aio::gpu::executor>();
    REQUIRE(exec != nullptr);

    // Calling stop() should not crash (no event loop running).
    exec->stop();
}
