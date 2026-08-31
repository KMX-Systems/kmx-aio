/// @file aio/benchmark/harness.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/benchmark/harness.hpp>

#include <algorithm>
#include <cmath>
#include <coroutine>
#include <cstdio>
#include <exception>
#include <print>

namespace kmx::aio::benchmark
{
    namespace detail
    {
        /// @brief Detached driver coroutine used to await a task from ordinary code.
        struct driver
        {
            struct promise_type
            {
                driver get_return_object() noexcept { return driver {std::coroutine_handle<promise_type>::from_promise(*this)}; }
                std::suspend_always initial_suspend() const noexcept { return {}; }

                struct final_awaiter
                {
                    bool await_ready() const noexcept { return false; }
                    void await_suspend(std::coroutine_handle<promise_type> h) const noexcept { h.destroy(); }
                    void await_resume() const noexcept {}
                };

                final_awaiter final_suspend() const noexcept { return {}; }
                void unhandled_exception() noexcept { std::terminate(); }
                void return_void() const noexcept {}
            };

            std::coroutine_handle<promise_type> handle;
        };

        static driver make_driver(task<void> t) noexcept(false)
        {
            co_await t;
        }
    } // namespace detail

    void registry::add(const std::string_view name, const case_fn_t run) noexcept(false)
    {
        cases_.push_back(case_entry {name, run});
    }

    std::size_t scaled(const std::size_t base, const double scale) noexcept
    {
        const auto value = static_cast<double>(base) * scale;
        if (value < 1.0)
            return 1u;

        return static_cast<std::size_t>(value);
    }

    result from_total(std::string name, const std::size_t operations, const clock_t::duration total) noexcept(false)
    {
        const auto elapsed_ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(total).count());
        result out {};
        out.name = std::move(name);
        out.operations = operations;
        out.mean_ns = (operations == 0u) ? 0.0 : (elapsed_ns / static_cast<double>(operations));
        return out;
    }

    result from_samples(std::string name, std::vector<double>& samples_ns) noexcept(false)
    {
        result out {};
        out.name = std::move(name);
        out.operations = samples_ns.size();
        if (samples_ns.empty())
            return out;

        std::sort(samples_ns.begin(), samples_ns.end());

        double sum {};
        for (const auto sample: samples_ns)
            sum += sample;

        const auto last = samples_ns.size() - 1u;
        out.mean_ns = sum / static_cast<double>(samples_ns.size());
        out.min_ns = samples_ns.front();
        out.p50_ns = samples_ns[last / 2u];
        out.p99_ns = samples_ns[static_cast<std::size_t>(static_cast<double>(last) * 0.99)];
        out.has_distribution = true;
        return out;
    }

    result skipped(std::string name, std::string reason) noexcept(false)
    {
        result out {};
        out.name = std::move(name);
        out.note = std::move(reason);
        out.skipped = true;
        return out;
    }

    void run_sync(task<void>&& t) noexcept
    {
        const auto d = detail::make_driver(std::move(t));
        d.handle.resume();
    }

    void print_results(const std::vector<result>& results) noexcept
    {
        std::size_t name_width = 4u;
        for (const auto& item: results)
            name_width = std::max(name_width, item.name.size());

        std::println("{:<{}}  {:>12}  {:>12}  {:>12}  {:>12}  {:>12}  {:>14}", "case", name_width, "ops", "mean ns", "min ns", "p50 ns",
                     "p99 ns", "ops/s");
        std::println("{:-<{}}", "", name_width + 88u);

        for (const auto& item: results)
        {
            if (item.skipped)
            {
                std::println("{:<{}}  {:>12}  {}", item.name, name_width, "skipped", item.note);
                continue;
            }

            const auto rate = (item.mean_ns > 0.0) ? (1e9 / item.mean_ns) : 0.0;
            if (item.has_distribution)
                std::println("{:<{}}  {:>12}  {:>12.1f}  {:>12.1f}  {:>12.1f}  {:>12.1f}  {:>14.0f}", item.name, name_width,
                             item.operations, item.mean_ns, item.min_ns, item.p50_ns, item.p99_ns, rate);
            else
                std::println("{:<{}}  {:>12}  {:>12.1f}  {:>12}  {:>12}  {:>12}  {:>14.0f}", item.name, name_width, item.operations,
                             item.mean_ns, "-", "-", "-", rate);

            if (!item.note.empty())
                std::println("{:<{}}  {}", "", name_width, item.note);
        }

        std::fflush(stdout);
    }

} // namespace kmx::aio::benchmark
