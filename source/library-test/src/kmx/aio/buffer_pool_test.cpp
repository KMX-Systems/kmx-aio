/// @file aio/buffer_pool_test.cpp
/// @brief Integration tests for buffer_pool and buffer_handle.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/buffer_pool.hpp>

#include <string>
#include <thread>
#include <vector>

namespace
{
    inline int unstable_buffer_ctor_calls = 0;

    struct unstable_buffer
    {
        unstable_buffer()
        {
            ++unstable_buffer_ctor_calls;
            if (unstable_buffer_ctor_calls == 1)
                throw std::runtime_error("constructor failed");
        }
    };
}

TEST_CASE("buffer_pool initializes with full capacity", "[buffer_pool][initialization]")
{
    kmx::aio::buffer_pool<int, 10> pool;

    REQUIRE(pool.capacity() == 10u);
    REQUIRE(pool.available() == 10u);
    REQUIRE(pool.allocated() == 0u);
    REQUIRE(pool.is_empty());
    REQUIRE_FALSE(pool.is_full());
}

TEST_CASE("buffer_pool acquires and releases single buffer", "[buffer_pool][acquire][release]")
{
    kmx::aio::buffer_pool<int, 10> pool;

    {
        auto handle = pool.acquire();
        REQUIRE(handle.valid());
        REQUIRE(pool.allocated() == 1u);
        REQUIRE(pool.available() == 9u);

        // Use the buffer
        *handle = 42;
        REQUIRE(*handle == 42);
    } // handle destroyed → buffer returned

    REQUIRE(pool.allocated() == 0u);
    REQUIRE(pool.available() == 10u);
}

TEST_CASE("buffer_pool tracks allocation state correctly", "[buffer_pool][statistics]")
{
    kmx::aio::buffer_pool<std::string, 5> pool;

    std::vector<kmx::aio::buffer_handle<std::string>> handles;

    // Acquire progressively
    for (int i = 0; i < 5; ++i)
    {
        handles.push_back(pool.acquire());
        REQUIRE(pool.allocated() == static_cast<std::size_t>(i + 1));
        REQUIRE(pool.available() == 5u - static_cast<std::size_t>(i + 1));
    }

    REQUIRE(pool.is_full());
    REQUIRE_FALSE(pool.is_empty());

    // Release progressively
    for (int i = 0; i < 5; ++i)
    {
        handles.pop_back();
        REQUIRE(pool.allocated() == 5u - static_cast<std::size_t>(i + 1));
        REQUIRE(pool.available() == static_cast<std::size_t>(i + 1));
    }

    REQUIRE(pool.is_empty());
}

TEST_CASE("buffer_pool throws when exhausted", "[buffer_pool][exhaustion]")
{
    kmx::aio::buffer_pool<int, 2> pool;

    auto h1 = pool.acquire();
    REQUIRE(pool.allocated() == 1u);

    auto h2 = pool.acquire();
    REQUIRE(pool.allocated() == 2u);
    REQUIRE(pool.is_full());

    // Third acquisition should throw
    REQUIRE_THROWS_AS(pool.acquire(), std::runtime_error);
}

TEST_CASE("buffer_pool preserves free-list on constructor throw", "[buffer_pool][exception-safety]")
{
    unstable_buffer_ctor_calls = 0;
    kmx::aio::buffer_pool<unstable_buffer, 2> pool;

    REQUIRE_THROWS_AS(pool.acquire(), std::runtime_error);
    REQUIRE(pool.allocated() == 0u);
    REQUIRE(pool.available() == 2u);

    auto handle = pool.acquire();
    REQUIRE(handle.valid());
    REQUIRE(pool.allocated() == 1u);
    REQUIRE(pool.available() == 1u);
}

TEST_CASE("buffer_pool supports complex types", "[buffer_pool][types]")
{
    struct complex_buffer
    {
        std::string name;
        std::vector<int> data;
        int count = 0;

        complex_buffer() noexcept: count(0) {}
    };

    kmx::aio::buffer_pool<complex_buffer, 5> pool;

    {
        auto handle = pool.acquire();
        REQUIRE(handle.valid());

        handle->name = "test";
        handle->data.push_back(123);
        handle->count = 42;

        REQUIRE(handle->name == "test");
        REQUIRE(handle->data.size() == 1u);
        REQUIRE(handle->count == 42);
    }

    REQUIRE(pool.is_empty());
}

TEST_CASE("buffer_handle move semantics", "[buffer_pool][buffer_handle][move]")
{
    kmx::aio::buffer_pool<int, 10> pool;

    {
        auto h1 = pool.acquire();
        *h1 = 42;
        REQUIRE(h1.valid());
        REQUIRE(pool.allocated() == 1u);

        // Move h1 to h2
        auto h2 = std::move(h1);
        REQUIRE_FALSE(h1.valid());
        REQUIRE(h2.valid());
        REQUIRE(*h2 == 42);
        REQUIRE(pool.allocated() == 1u); // Still only one buffer

        // h1 destroyed (empty) → no-op
        // h2 destroyed → buffer released
    }

    REQUIRE(pool.is_empty());
}

TEST_CASE("buffer_handle arrow operator", "[buffer_pool][buffer_handle]")
{
    struct point
    {
        int x = 0;
        int y = 0;
    };

    kmx::aio::buffer_pool<point, 5> pool;

    {
        auto handle = pool.acquire();

        // Arrow operator for member access
        handle->x = 10;
        handle->y = 20;

        REQUIRE(handle->x == 10);
        REQUIRE(handle->y == 20);
    }

    REQUIRE(pool.is_empty());
}

TEST_CASE("buffer_handle explicit reset", "[buffer_pool][buffer_handle][reset]")
{
    kmx::aio::buffer_pool<int, 10> pool;

    auto handle = pool.acquire();
    REQUIRE(pool.allocated() == 1u);
    REQUIRE(handle.valid());

    // Explicit reset before destruction
    handle.reset();
    REQUIRE_FALSE(handle.valid());
    REQUIRE(pool.is_empty());

    // Calling reset on an already-reset handle is safe
    handle.reset();
    REQUIRE_FALSE(handle.valid());
}

TEST_CASE("buffer_handle default constructed is invalid", "[buffer_pool][buffer_handle]")
{
    kmx::aio::buffer_handle<int> handle;

    REQUIRE_FALSE(handle.valid());
    REQUIRE(handle.get() == nullptr);

    // Accessing invalid handle throws
    REQUIRE_THROWS_AS(*handle, std::logic_error);
    REQUIRE_THROWS_AS(handle.operator->(), std::logic_error);
}

TEST_CASE("buffer_pool zero-copy reuse", "[buffer_pool][zero-copy]")
{
    // Simulate a zero-copy scenario where buffers are reused for I/O
    kmx::aio::buffer_pool<std::vector<std::byte>, 3> pool;

    // Round 1: acquire all buffers
    {
        std::vector<kmx::aio::buffer_handle<std::vector<std::byte>>> handles;

        for (int i = 0; i < 3; ++i)
        {
            auto h = pool.acquire();
            h->resize(1024);
            handles.push_back(std::move(h));
        }

        REQUIRE(pool.is_full());

        // All buffers in use
        for (const auto& h: handles)
        {
            REQUIRE(h->size() == 1024u);
        }
    } // All handles destroyed → buffers returned

    REQUIRE(pool.is_empty());

    // Round 2: acquire again (same memory reused)
    {
        auto h = pool.acquire();
        // Buffer has been destructed and reconstructed
        REQUIRE(h->size() == 0u); // Freshly constructed
        h->resize(512);
        REQUIRE(h->size() == 512u);
    }

    REQUIRE(pool.is_empty());
}

TEST_CASE("buffer_pool interleaved acquire-release", "[buffer_pool][fragmentation]")
{
    kmx::aio::buffer_pool<int, 100> pool;

    // Allocate in bursts, release randomly
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        std::vector<kmx::aio::buffer_handle<int>> handles;

        // Allocate 50 buffers
        for (int i = 0; i < 50; ++i)
        {
            handles.push_back(pool.acquire());
        }

        REQUIRE(pool.allocated() == 50u);
        REQUIRE(pool.available() == 50u);

        // Release alternately (pattern: keep, release, keep, release, ...)
        std::vector<kmx::aio::buffer_handle<int>> kept;
        for (std::size_t i = 0; i < handles.size(); ++i)
        {
            if (i % 2 == 0)
                kept.push_back(std::move(handles[i]));
            // else: moved-from handles remain in vector
        }

        // Clear the vector containing moved-from handles (the odd-indexed originals)
        handles.clear();

        REQUIRE(pool.allocated() == 25u);

        // Release all kept handles
        kept.clear();

        REQUIRE(pool.is_empty());
    }
}

TEST_CASE("buffer_pool thread-safe concurrent acquire/release", "[buffer_pool][thread-safety]")
{
    kmx::aio::buffer_pool<int, 50> pool;
    std::atomic<int> errors = 0;
    std::atomic<int> value_mismatches = 0;

    auto worker = [&](int id)
    {
        try
        {
            for (int i = 0; i < 100; ++i)
            {
                auto handle = pool.acquire();
                *handle = id * 1000 + i;

                // Simulate some work
                std::this_thread::yield();

                if (*handle != id * 1000 + i)
                    value_mismatches.fetch_add(1, std::memory_order_relaxed);
            }
        }
        catch (const std::exception&)
        {
            // Expected when pool exhausted
            errors.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // Spawn multiple threads
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; ++i)
    {
        threads.emplace_back(worker, i);
    }

    for (auto& t: threads)
    {
        t.join();
    }

    // Pool should be empty after all threads complete
    REQUIRE(pool.is_empty());
    REQUIRE(value_mismatches.load(std::memory_order_relaxed) == 0);

    // Some threads may have hit exhaustion, which is OK
    // (just verifies thread-safety, not unlimited allocation)
}

TEST_CASE("buffer_pool const access", "[buffer_pool][buffer_handle]")
{
    kmx::aio::buffer_pool<std::string, 5> pool;

    auto handle = pool.acquire();
    *handle = "hello";

    // Const dereference
    const auto& str_const = static_cast<const decltype(handle)&>(handle);
    REQUIRE(*str_const == "hello");
    REQUIRE(str_const->size() == 5u);

    // Const pointer access
    const std::string* ptr = str_const.operator->();
    REQUIRE(*ptr == "hello");
}
