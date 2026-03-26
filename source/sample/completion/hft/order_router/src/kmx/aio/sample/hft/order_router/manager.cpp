#include <kmx/aio/sample/hft/order_router/manager.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

namespace kmx::aio::sample::hft::order_router
{
    // Shared channel: producer pushes, consumer pops.
    kmx::aio::channel<order> order_channel(channel_capacity);

    // Atomic flag signalling the producer has finished.
    std::atomic_bool producer_done {false};

    // Stats collected by the consumer.
    std::atomic_uint64_t filled_count {};
    std::atomic_uint64_t rejected_count {};

    // Market-data producer (Core 2)
    void market_data_thread() noexcept
    {
        using clock = std::chrono::steady_clock;

        const auto start = clock::now();

        for (std::uint64_t i {}; i < total_orders; ++i)
        {
            order o {
                .id        = i,
                .direction = (i % 2u == 0u) ? side::buy : side::sell,
                .price     = 100.0 + static_cast<double>(i % 50u) * 0.25,
                .quantity  = static_cast<std::uint32_t>((i % 10u) + 1u) * 100u,
            };

            // Spin until there is room in the channel (back-pressure).
            while (!order_channel.try_push(std::move(o)))
                std::this_thread::yield();
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - start);

        std::cout << "[market-data] produced " << total_orders << " orders in "
                  << elapsed.count() << " µs ("
                  << (total_orders * 1'000'000ULL / static_cast<std::uint64_t>(elapsed.count()))
                  << " orders/sec)\n";

        producer_done.store(true, std::memory_order_release);
    }

    // Strategy consumer (Core 4)
    void strategy_thread() noexcept
    {
        using clock = std::chrono::steady_clock;

        const auto start = clock::now();
        std::uint64_t consumed {};

        while (true)
        {
            if (auto maybe_order = order_channel.try_pop())
            {
                // Trivial fill/reject: reject every 7th order.
                if (maybe_order->id % 7u == 0u)
                    rejected_count.fetch_add(1u, std::memory_order_relaxed);
                else
                    filled_count.fetch_add(1u, std::memory_order_relaxed);

                ++consumed;
            }
            else if (producer_done.load(std::memory_order_acquire))
            {
                // Drain any remaining items before exiting.
                while (auto trailing = order_channel.try_pop())
                {
                    if (trailing->id % 7u == 0u)
                        rejected_count.fetch_add(1u, std::memory_order_relaxed);
                    else
                        filled_count.fetch_add(1u, std::memory_order_relaxed);

                    ++consumed;
                }
                break;
            }
            else
                std::this_thread::yield();
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - start);

        std::cout << "[strategy]    consumed " << consumed << " orders in "
                  << elapsed.count() << " µs ("
                  << (consumed * 1'000'000ULL / static_cast<std::uint64_t>(elapsed.count()))
                  << " orders/sec)\n";
    }

    // CPU pinning helper (best-effort)
    void pin_thread(std::thread& t, const int core_id) noexcept
    {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset);
    }

    auto execute_order_router() -> int
    {
        std::cout << "╔══════════════════════════════════════════════════════════╗\n"
                  << "║   KMX AIO · Phase 5 · HFT Order Router                 ║\n"
                  << "║   Lockless SPSC channel between CPU-pinned threads      ║\n"
                  << "╚══════════════════════════════════════════════════════════╝\n\n";

        std::cout << "Channel capacity .... " << order_channel.capacity() << " slots\n"
                  << "Orders to route ..... " << total_orders << "\n\n";

        // Spawn producer and consumer, pinned to distinct cores.
        std::thread producer(market_data_thread);
        std::thread consumer(strategy_thread);

        // Best-effort core pinning (requires sufficient cores on the host).
        pin_thread(producer, 2);
        pin_thread(consumer, 4);

        producer.join();
        consumer.join();

        // Summary
        const auto filled   = filled_count.load();
        const auto rejected = rejected_count.load();

        std::cout << "\n┌────────────────────────────────────────┐\n"
                  << "│ Results                                │\n"
                  << "├────────────────────────────────────────┤\n"
                  << "│ Filled    : " << filled   << std::string(25 - std::to_string(filled).size(), ' ') << "│\n"
                  << "│ Rejected  : " << rejected << std::string(25 - std::to_string(rejected).size(), ' ') << "│\n"
                  << "│ Total     : " << (filled + rejected) << std::string(25 - std::to_string(filled + rejected).size(), ' ') << "│\n"
                  << "└────────────────────────────────────────┘\n";

        if ((filled + rejected) != total_orders)
        {
            std::cerr << "[ERROR] Order count mismatch!\n";
            return 1;
        }

        std::cout << "\n✔ All " << total_orders << " orders routed with zero contention.\n";
        return 0;
    }

} // namespace kmx::aio::sample::hft::order_router
