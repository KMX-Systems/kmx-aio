/// @file aio/benchmark/core_cases.cpp
/// @brief Coroutine, allocator, channel and buffer-pool micro-benchmarks.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/benchmark/cases.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

#include <kmx/aio/allocator/slab.hpp>
#include <kmx/aio/buffer/pool.hpp>
#include <kmx/aio/channel.hpp>
#include <kmx/aio/scheduler.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::benchmark
{
    namespace core_detail
    {
        static task<std::uint64_t> leaf(const std::uint64_t value) noexcept(false)
        {
            co_return value;
        }

        static task<std::uint64_t> chain(const std::uint64_t value, const unsigned depth) noexcept(false)
        {
            if (depth == 0u)
                co_return value;

            co_return co_await chain(value + 1u, depth - 1u);
        }

        static task<void> await_leaf_body(const std::size_t iterations, std::uint64_t& sink) noexcept(false)
        {
            std::uint64_t total {};
            for (std::size_t i {}; i != iterations; ++i)
                total += co_await leaf(i);

            sink = total;
        }

        static task<void> await_chain_body(const std::size_t iterations, const unsigned depth, std::uint64_t& sink) noexcept(false)
        {
            std::uint64_t total {};
            for (std::size_t i {}; i != iterations; ++i)
                total += co_await chain(i, depth);

            sink = total;
        }

        /// @brief Times a coroutine-await loop, optionally with a slab allocator installed.
        static result measure_await(std::string name, const std::size_t iterations, const unsigned depth, const bool use_slab)
        {
            allocator::slab slab {1024u, 64u};
            if (use_slab)
                set_thread_allocator(&slab);

            std::uint64_t sink {};
            // Warm-up, so the first run does not pay for lazily faulted pages.
            run_sync(await_chain_body(iterations / 16u + 1u, depth, sink));

            const auto start = clock_t::now();
            if (depth == 0u)
                run_sync(await_leaf_body(iterations, sink));
            else
                run_sync(await_chain_body(iterations, depth, sink));

            const auto elapsed = clock_t::now() - start;
            keep(sink);
            set_thread_allocator(nullptr);

            const auto awaits = (depth == 0u) ? iterations : (iterations * (depth + 1u));
            return from_total(std::move(name), awaits, elapsed);
        }
    } // namespace core_detail

    static result bench_task_await_heap(const double scale)
    {
        return core_detail::measure_await("core/task_await (heap frames)", scaled(2'000'000u, scale), 0u, false);
    }

    static result bench_task_await_slab(const double scale)
    {
        return core_detail::measure_await("core/task_await (slab frames)", scaled(2'000'000u, scale), 0u, true);
    }

    static result bench_task_chain_slab(const double scale)
    {
        return core_detail::measure_await("core/task_await_chain8 (slab)", scaled(250'000u, scale), 8u, true);
    }

    static result bench_slab_alloc(const double scale)
    {
        const auto iterations = scaled(20'000'000u, scale);
        allocator::slab slab {256u, 64u};

        const auto start = clock_t::now();
        for (std::size_t i {}; i != iterations; ++i)
        {
            void* const p = slab.allocate();
            keep(p);
            slab.deallocate(p);
        }

        const auto elapsed = clock_t::now() - start;
        return from_total("core/slab_allocate+deallocate", iterations, elapsed);
    }

    static result bench_channel_same_thread(const double scale)
    {
        const auto iterations = scaled(20'000'000u, scale);
        channel<std::uint64_t> ch {1024u};

        const auto start = clock_t::now();
        for (std::size_t i {}; i != iterations; ++i)
        {
            const bool pushed = ch.try_push(std::uint64_t {i});
            keep(pushed);
            auto value = ch.try_pop();
            keep(value);
        }

        const auto elapsed = clock_t::now() - start;
        return from_total("core/channel_push+pop (same thread)", iterations, elapsed);
    }

    static result bench_channel_cross_thread(const double scale)
    {
        const auto iterations = scaled(5'000'000u, scale);
        channel<std::uint64_t> ch {4096u};
        std::atomic_bool go {};

        std::jthread consumer {[&ch, &go, iterations]() noexcept
                               {
                                   go.wait(false, std::memory_order_acquire);
                                   for (std::size_t received {}; received != iterations;)
                                   {
                                       if (ch.try_pop())
                                           ++received;
                                   }
                               }};

        go.store(true, std::memory_order_release);
        go.notify_all();

        const auto start = clock_t::now();
        for (std::size_t i {}; i != iterations;)
        {
            if (ch.try_push(std::uint64_t {i}))
                ++i;
        }

        consumer.join();
        const auto elapsed = clock_t::now() - start;
        return from_total("core/channel_transfer (2 threads)", iterations, elapsed);
    }

    static result bench_buffer_pool(const double scale)
    {
        const auto iterations = scaled(10'000'000u, scale);
        buffer::pool<std::array<std::byte, 256u>, 64u> pool {};

        const auto start = clock_t::now();
        for (std::size_t i {}; i != iterations; ++i)
        {
            auto handle = pool.try_acquire();
            keep(handle);
        }

        const auto elapsed = clock_t::now() - start;
        return from_total("core/buffer_pool_acquire+release", iterations, elapsed);
    }

    static result bench_scheduler_dispatch(const double scale)
    {
        const auto iterations = scaled(200'000u, scale);
        scheduler sched {1u};
        std::atomic_uint64_t done {};

        const auto start = clock_t::now();
        for (std::size_t i {}; i != iterations; ++i)
            sched.spawn([&done]() noexcept { done.fetch_add(1u, std::memory_order_relaxed); });

        sched.wait_until_idle();
        const auto elapsed = clock_t::now() - start;
        keep(done.load(std::memory_order_relaxed));
        return from_total("core/scheduler_spawn+run", iterations, elapsed);
    }

    void register_core_cases(registry& reg) noexcept(false)
    {
        reg.add("core/task_await_heap", bench_task_await_heap);
        reg.add("core/task_await_slab", bench_task_await_slab);
        reg.add("core/task_await_chain8", bench_task_chain_slab);
        reg.add("core/slab_allocate", bench_slab_alloc);
        reg.add("core/channel_same_thread", bench_channel_same_thread);
        reg.add("core/channel_cross_thread", bench_channel_cross_thread);
        reg.add("core/buffer_pool", bench_buffer_pool);
        reg.add("core/scheduler_dispatch", bench_scheduler_dispatch);
    }

} // namespace kmx::aio::benchmark
