/// @file aio/benchmark/harness.cpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/benchmark/harness.hpp>

#include <algorithm>
#include <cmath>
#include <coroutine>
#include <cstdio>
#include <exception>
#include <format>
#include <print>
#include <string>
#include <string_view>

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

        /// @brief Fewest samples a 99th percentile is worth reporting from. Below it the figure names
        ///        one of the handful of slowest operations, which is not what a reader takes it for.
        static constexpr std::size_t min_samples_for_p99 = 100u;

        /// @brief Index of a percentile by nearest rank.
        /// @param count The number of samples, which must not be zero.
        /// @param fraction The percentile, as a fraction of one.
        /// @return The index into the sorted samples.
        static std::size_t rank_index(const std::size_t count, const double fraction) noexcept
        {
            const auto rank = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(count)));
            return (rank == 0u) ? 0u : std::min(rank - 1u, count - 1u);
        }
    } // namespace detail

    void registry::add(const std::string_view name, const case_fn_t run) noexcept(false)
    {
        cases_.push_back(case_entry {name, run});
    }

    void registry::describe(const std::string_view group, const std::string_view description) noexcept(false)
    {
        groups_.push_back(group_entry {group, description});
    }

    std::string_view registry::description(const std::string_view group) const noexcept
    {
        for (const auto& item: groups_)
            if (item.name == group)
                return item.description;

        return {};
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

        const auto count = samples_ns.size();
        out.mean_ns = sum / static_cast<double>(count);
        out.min_ns = samples_ns.front();
        out.p50_ns = samples_ns[detail::rank_index(count, 0.50)];
        out.p99_ns = samples_ns[detail::rank_index(count, 0.99)];
        out.has_distribution = true;
        out.has_p99 = (count >= detail::min_samples_for_p99);
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

    namespace detail
    {
        /// @brief Separator printed between two columns.
        static constexpr std::string_view column_gap = "  ";

        /// @brief Field width of a duration column, wide enough for "1.23 ms".
        static constexpr std::size_t time_width = 8u;

        /// @brief Field width of the rate column, wide enough for "1.23 G/s".
        static constexpr std::size_t rate_width = 9u;

        /// @brief Field width of the operation-count column, wide enough for "999,999,999".
        static constexpr std::size_t count_width = 11u;

        /// @brief Indent of a case name below its group heading.
        static constexpr std::size_t row_indent = 2u;

        /// @brief Widest the note column is allowed to become. Past it a note wraps inside the column
        ///        rather than running the table off the side of any reasonable terminal.
        static constexpr std::size_t note_width_cap = 110u;

        /// @brief Number of terminal cells a UTF-8 string occupies.
        /// @param text The text to measure.
        /// @return The character count, which is not the byte count once a unit like "µs" is spelled properly.
        static std::size_t width_of(const std::string_view text) noexcept
        {
            std::size_t width {};
            for (const auto c: text)
                width += ((static_cast<unsigned char>(c) & 0xC0u) == 0x80u) ? 0u : 1u;

            return width;
        }

        /// @brief Right-aligns text in a field, counting characters rather than bytes.
        /// @param text The text to align.
        /// @param width The field width.
        /// @return The padded text.
        /// @throws std::bad_alloc if the result cannot be stored.
        static std::string right(const std::string_view text, const std::size_t width) noexcept(false)
        {
            const auto used = width_of(text);
            std::string out((used < width) ? (width - used) : 0u, ' ');
            out += text;
            return out;
        }

        /// @brief Appends one right-aligned column to a line.
        /// @param line The line being built.
        /// @param text The cell contents.
        /// @param width The field width.
        /// @throws std::bad_alloc if the line cannot grow.
        static void add_column(std::string& line, const std::string_view text, const std::size_t width) noexcept(false)
        {
            line += column_gap;
            line += right(text, width);
        }

        /// @brief Appends the last column of a line, left-aligned and with no padding after it.
        /// @param line The line being built.
        /// @param text The cell contents.
        /// @throws std::bad_alloc if the line cannot grow.
        static void add_last_column(std::string& line, const std::string_view text) noexcept(false)
        {
            line += column_gap;
            line += text;
        }

        /// @brief Formats a nanosecond figure with three significant digits and the unit it reads best in.
        /// @param ns The figure, in nanoseconds.
        /// @return The formatted figure, e.g. "24.2 ns" or "4.93 µs".
        /// @throws std::bad_alloc if the result cannot be stored.
        static std::string duration_text(const double ns) noexcept(false)
        {
            auto value = ns;
            std::string_view unit = "ns";
            if (value >= 1e6)
            {
                value /= 1e6;
                unit = "ms";
            }
            else if (value >= 1e3)
            {
                value /= 1e3;
                unit = "µs";
            }

            const auto precision = (value < 10.0) ? 2 : ((value < 100.0) ? 1 : 0);
            return std::format("{:.{}f} {}", value, precision, unit);
        }

        /// @brief Formats a rate with three significant digits and an SI prefix.
        /// @param per_second Operations per second.
        /// @return The formatted rate, e.g. "41.3 M/s".
        /// @throws std::bad_alloc if the result cannot be stored.
        static std::string rate_text(const double per_second) noexcept(false)
        {
            auto value = per_second;
            std::string_view prefix {};
            if (value >= 1e9)
            {
                value /= 1e9;
                prefix = " G";
            }
            else if (value >= 1e6)
            {
                value /= 1e6;
                prefix = " M";
            }
            else if (value >= 1e3)
            {
                value /= 1e3;
                prefix = " k";
            }

            const auto precision = prefix.empty() ? 0 : ((value < 10.0) ? 2 : ((value < 100.0) ? 1 : 0));
            return std::format("{:.{}f}{}/s", value, precision, prefix);
        }

        /// @brief Formats an operation count in groups of three digits.
        /// @param value The count.
        /// @return The grouped count, e.g. "20,000,000".
        /// @throws std::bad_alloc if the result cannot be stored.
        static std::string count_text(const std::size_t value) noexcept(false)
        {
            const auto digits = std::format("{}", value);
            std::string out {};
            for (std::size_t i {}; i != digits.size(); ++i)
            {
                if ((i != 0u) && (((digits.size() - i) % 3u) == 0u))
                    out += ',';

                out += digits[i];
            }

            return out;
        }

        /// @brief Breaks a note into lines that fit the note column, on word boundaries.
        /// @param note The note text.
        /// @param width The note column width.
        /// @return The lines, in order. Empty when the note is empty.
        /// @throws std::bad_alloc if the lines cannot be stored.
        static std::vector<std::string_view> wrapped(std::string_view note, const std::size_t width) noexcept(false)
        {
            std::vector<std::string_view> lines {};
            while (!note.empty())
            {
                auto take = note.size();
                if (take > width)
                {
                    const auto space = note.rfind(' ', width);
                    take = (space == std::string_view::npos) ? width : space;
                }

                lines.push_back(note.substr(0u, take));
                note.remove_prefix(take);
                while (!note.empty() && (note.front() == ' '))
                    note.remove_prefix(1u);
            }

            return lines;
        }

        /// @brief The part of a case name before the '/', or nothing when it has none.
        /// @param name The registered case name.
        /// @return The group name.
        static std::string_view group_of(const std::string_view name) noexcept
        {
            const auto pos = name.find('/');
            return (pos == std::string_view::npos) ? std::string_view {} : name.substr(0u, pos);
        }

        /// @brief The part of a case name after the '/', which is what the row shows.
        /// @param name The registered case name.
        /// @return The case name without its group.
        static std::string_view case_of(const std::string_view name) noexcept
        {
            const auto pos = name.find('/');
            return (pos == std::string_view::npos) ? name : name.substr(pos + 1u);
        }
    } // namespace detail

    void print_results(const std::vector<result>& results, const registry& reg) noexcept
    {
        using namespace detail;

        std::size_t name_width = 4u;
        std::size_t note_width {};
        for (const auto& item: results)
        {
            name_width = std::max(name_width, case_of(item.name).size() + row_indent);
            note_width = std::max(note_width, item.note.size());
        }

        note_width = std::min(note_width, note_width_cap);

        auto header = std::format("{:<{}}", "case", name_width);
        add_column(header, "mean", time_width);
        add_column(header, "min", time_width);
        add_column(header, "p50", time_width);
        add_column(header, "p99", time_width);
        add_column(header, "rate", rate_width);
        add_column(header, "ops", count_width);

        // The rule spans the note column as well, which the header itself only starts.
        const auto columns_width = width_of(header);
        const auto rule_width = (note_width == 0u) ? columns_width : (columns_width + column_gap.size() + note_width);
        if (note_width != 0u)
            add_last_column(header, "what it means");

        std::println("{}", header);
        std::println("{:-<{}}", "", rule_width);

        std::string_view group = "\x01"; // A group name no case can have, so the first case opens a section.
        for (const auto& item: results)
        {
            const auto item_group = group_of(item.name);
            if (item_group != group)
            {
                group = item_group;
                std::println("");
                if (!group.empty())
                {
                    const auto description = reg.description(group);
                    if (description.empty())
                        std::println("{}", group);
                    else
                        std::println("{} - {}", group, description);
                }
            }

            auto line = std::format("{:{}}{:<{}}", "", row_indent, case_of(item.name), name_width - row_indent);
            if (item.skipped)
            {
                // The numeric columns stay empty, so the reason lands under the note column like any other remark.
                add_column(line, "skipped", time_width);
                add_column(line, "", time_width);
                add_column(line, "", time_width);
                add_column(line, "", time_width);
                add_column(line, "", rate_width);
                add_column(line, "", count_width);
            }
            else
            {
                add_column(line, duration_text(item.mean_ns), time_width);
                if (item.has_distribution)
                {
                    add_column(line, duration_text(item.min_ns), time_width);
                    add_column(line, duration_text(item.p50_ns), time_width);
                    add_column(line, item.has_p99 ? duration_text(item.p99_ns) : std::string {"-"}, time_width);
                }
                else
                {
                    add_column(line, "-", time_width);
                    add_column(line, "-", time_width);
                    add_column(line, "-", time_width);
                }

                add_column(line, rate_text((item.mean_ns > 0.0) ? (1e9 / item.mean_ns) : 0.0), rate_width);
                add_column(line, count_text(item.operations), count_width);
            }

            const auto note_lines = wrapped(item.note, note_width);
            if (!note_lines.empty())
                add_last_column(line, note_lines.front());

            std::println("{}", line);

            // A note too long for the column carries on down it, under its own first line.
            for (std::size_t i = 1u; i < note_lines.size(); ++i)
                std::println("{:{}}{}{}", "", columns_width, column_gap, note_lines[i]);
        }

        std::println("");
        std::println("{:-<{}}", "", rule_width);
        std::println("mean, min, p50 and p99 are the cost of one operation; rate is 1 s / mean; ops is how many were measured.");
        std::println("A \"-\" means the case timed the whole loop rather than each operation, so it has no distribution to report,");
        std::println("or - under p99 alone - that it took fewer than {} samples, too few for a percentile to name anything.",
                     detail::min_samples_for_p99);
        std::fflush(stdout);
    }

} // namespace kmx::aio::benchmark
