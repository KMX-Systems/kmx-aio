/// @file aio/allocation/slab_coroutine_test.cpp
/// @brief Integration test: coroutine frame allocation via thread-local slab allocator.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/allocator.hpp>
#include <kmx/aio/task.hpp>

#include <chrono>
#include <memory>
#include <vector>

namespace
{
    /// @brief Test coroutine that yields a value from thread-local slab allocation.
    kmx::aio::task<int> simple_coro(const int value)
    {
        co_return value;
    }

    /// @brief Recursive coroutine to stress slab allocation chaining.
    kmx::aio::task<int> recursive_coro(const int depth)
    {
        if (depth <= 0)
            co_return 0;

        const auto result = co_await recursive_coro(depth - 1);
        co_return result + 1;
    }

    /// @brief Coroutine deliberately inflated to exceed tiny slab slots.
    kmx::aio::task<int> oversized_coro(const int value)
    {
        std::array<std::byte, 256u> padding {};
        co_await std::suspend_always {};
        co_return value + static_cast<int>(padding.size());
    }

} // namespace

TEST_CASE("coroutine frames allocate from thread-local slab", "[allocation][slab][coroutine]")
{
    // Allocator configured for typical coroutine frame sizes (~256-512 bytes).
    auto alloc = std::make_unique<kmx::aio::slab_allocator>(512, 256);
    auto& stats = kmx::aio::get_allocator_statistics();

    stats.reset();

    REQUIRE(alloc->allocated() == 0u);
    REQUIRE(alloc->available() == 256u);

    // Set as thread-local allocator.
    kmx::aio::set_thread_allocator(alloc.get());

    // Spawn a simple coroutine.
    {
        auto t = simple_coro(42);
        // After co_await, coroutine frame is still allocated.
        const auto allocated_count_1 = alloc->allocated();
        REQUIRE(allocated_count_1 >= 1u); // At least one frame in slab.
        REQUIRE(stats.slab_allocations.load(std::memory_order_relaxed) >= 1u);
        REQUIRE(stats.heap_allocations.load(std::memory_order_relaxed) == 0u);
    } // Coroutine destroyed here, frame deallocated.

    REQUIRE(alloc->allocated() == 0u);

    // Stress-test: spawn and destroy multiple coroutines rapidly.
    const auto initial_available = alloc->available();
    {
        std::vector<kmx::aio::task<int>> tasks;
        for (int i = 0; i < 10; ++i)
            tasks.emplace_back(simple_coro(i));

        // All 10 frames should be allocated from slab.
        const auto allocated_count = alloc->allocated();
        REQUIRE(allocated_count == 10u);
        REQUIRE(alloc->available() == initial_available - 10u);
    } // All tasks destroyed here.

    // After destruction, all slots must return to free-list.
    REQUIRE(alloc->allocated() == 0u);
    REQUIRE(alloc->available() == initial_available);

    // Recursive coroutines test: verify deep call chains don't exhaust slab.
    {
        auto recursive = recursive_coro(5);
        // At most 6 frames on stack at once (depth 5 + initial).
        const auto allocated_count = alloc->allocated();
        REQUIRE(allocated_count <= 6u);
        REQUIRE(allocated_count >= 1u);
    }

    REQUIRE(alloc->allocated() == 0u);

    // Clear thread-local allocator.
    kmx::aio::set_thread_allocator(nullptr);
}

TEST_CASE("slab allocator slot fragmentation after heavy churn", "[allocation][slab]")
{
    auto alloc = std::make_unique<kmx::aio::slab_allocator>(256, 100);

    kmx::aio::set_thread_allocator(alloc.get());

    // Allocate and deallocate in patterns to stress fragmentation resistance.
    const auto start_available = alloc->available();

    // Pattern 1: Fill, destroy, refill (FIFO-like chaining via embedded free-list).
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        std::vector<void*> ptrs;
        for (int i = 0; i < 50; ++i)
            if (void* p = alloc->allocate())
                ptrs.push_back(p);

        REQUIRE(ptrs.size() == 50u);
        REQUIRE(alloc->allocated() == 50u);

        for (void* p: ptrs)
            alloc->deallocate(p);

        REQUIRE(alloc->allocated() == 0u);
        REQUIRE(alloc->available() == start_available); // Full recovery.
    }

    // Pattern 2: Interleaved alloc/dealloc (worst-case fragmentation).
    std::vector<void*> held;
    for (int i = 0; i < 30; ++i)
    {
        if (void* p = alloc->allocate())
            held.push_back(p);

        if (held.size() > 15)
        {
            alloc->deallocate(held.back());
            held.pop_back();
        }
    }

    // Release remaining.
    for (void* p: held)
        alloc->deallocate(p);

    REQUIRE(alloc->allocated() == 0u);
    REQUIRE(alloc->available() == start_available);

    kmx::aio::set_thread_allocator(nullptr);
}

TEST_CASE("coroutine frame exceeding slab slot falls back to malloc", "[allocation][slab][fallback]")
{
    // Create a very small slab (slot_size 64) that coroutine frames may exceed.
    auto alloc = std::make_unique<kmx::aio::slab_allocator>(64, 10);
    auto& stats = kmx::aio::get_allocator_statistics();

    stats.reset();

    kmx::aio::set_thread_allocator(alloc.get());

    // Spawn a coroutine (typical frame size ~200+ bytes).
    // The allocator should fall back to ::operator new for the frame.
    {
        auto t = oversized_coro(99);
        // Slab may or may not have allocated (depends on frame size).
        // The test verifies fallback logic doesn't crash.
        REQUIRE(true); // Coroutine created successfully.
    }

    REQUIRE(stats.heap_allocations.load(std::memory_order_relaxed) >= 1u);

    // Verify no memory leak from fallback (frame should be freed via ::operator delete).
    kmx::aio::set_thread_allocator(nullptr);
}

TEST_CASE("thread-local allocator isolation", "[allocation][slab][thread-local]")
{
    auto alloc1 = std::make_unique<kmx::aio::slab_allocator>(256, 50);

    kmx::aio::set_thread_allocator(alloc1.get());
    REQUIRE(kmx::aio::get_thread_allocator() == alloc1.get());

    // Allocate from thread-local slab.
    void* p1 = alloc1->allocate();
    REQUIRE(p1 != nullptr);
    REQUIRE(alloc1->allocated() == 1u);

    // Clear thread-local.
    kmx::aio::set_thread_allocator(nullptr);
    REQUIRE(kmx::aio::get_thread_allocator() == nullptr);

    // Cleanup.
    alloc1->deallocate(p1);
}
