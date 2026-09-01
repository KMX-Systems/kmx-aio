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

    void registry::add_paired(const std::string_view key, const execution_model model, const std::string_view name,
                              const case_fn_t run) noexcept(false)
    {
        cases_.push_back(case_entry {name, run, key, model});

        // The scenario is listed the first time either side mentions it, so the comparison keeps the
        // order the cases were registered in whichever side got there first.
        for (const auto& item: pairs_)
            if (item.key == key)
                return;

        pairs_.push_back(pair_entry {key, {}});
    }

    void registry::describe_pair(const std::string_view key, const std::string_view description) noexcept(false)
    {
        for (auto& item: pairs_)
            if (item.key == key)
            {
                item.description = description;
                return;
            }

        pairs_.push_back(pair_entry {key, description});
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

    std::string_view registry::pair_description(const std::string_view key) const noexcept
    {
        for (const auto& item: pairs_)
            if (item.key == key)
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

    result with_note(result outcome, std::string note) noexcept(false)
    {
        if (!outcome.skipped)
            outcome.note = std::move(note);

        return outcome;
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

        // Sections are collected by group rather than taken from adjacency: a paired scenario
        // registers one case in each of two groups, so registration order no longer keeps a group's
        // rows together. Groups appear in the order they are first seen and rows keep their order
        // within a group, which is what plain adjacency gave when every group was contiguous.
        std::vector<std::string_view> groups {};
        for (const auto& item: results)
        {
            const auto item_group = group_of(item.name);
            if (std::find(groups.begin(), groups.end(), item_group) == groups.end())
                groups.push_back(item_group);
        }

        for (const auto& group: groups)
        {
            std::println("");
            if (!group.empty())
            {
                const auto description = reg.description(group);
                if (description.empty())
                    std::println("{}", group);
                else
                    std::println("{} - {}", group, description);
            }

            for (const auto& item: results)
            {
                if (group_of(item.name) != group)
                    continue;

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
        }

        std::println("");
        std::println("{:-<{}}", "", rule_width);
        std::println("mean, min, p50 and p99 are the cost of one operation; rate is 1 s / mean; ops is how many were measured.");
        std::println("A \"-\" means the case timed the whole loop rather than each operation, so it has no distribution to report,");
        std::println("or - under p99 alone - that it took fewer than {} samples, too few for a percentile to name anything.",
                     detail::min_samples_for_p99);
        std::fflush(stdout);
    }

    namespace detail
    {
        /// @brief Field width of the delta column, wide enough for "+1234%".
        static constexpr std::size_t delta_width = 7u;

        /// @brief The figure a comparison row quotes for one side.
        /// @param item The result to read.
        /// @return The median where the case sampled each operation, the mean where it timed a whole loop.
        static double quoted_ns(const result& item) noexcept
        {
            return item.has_distribution ? item.p50_ns : item.mean_ns;
        }

        /// @brief Finds one side of a pairing among the results.
        /// @param results The results to search.
        /// @param key The scenario name.
        /// @param model The side wanted.
        /// @return The result, or nullptr when that side did not run - it was filtered out, or the
        ///         model is not in this build.
        static const result* side_of(const std::vector<result>& results, const std::string_view key, const execution_model model) noexcept
        {
            for (const auto& item: results)
                if ((item.pair_key == key) && (item.model == model))
                    return &item;

            return nullptr;
        }

        /// @brief Formats the change from the epoll figure to the io_uring one.
        /// @param readiness_ns The epoll figure.
        /// @param completion_ns The io_uring figure.
        /// @return The change as a signed percentage, negative where io_uring is the faster of the two.
        /// @throws std::bad_alloc if the result cannot be stored.
        static std::string delta_text(const double readiness_ns, const double completion_ns) noexcept(false)
        {
            if (readiness_ns <= 0.0)
                return "-";

            return std::format("{:+.0f}%", ((completion_ns - readiness_ns) / readiness_ns) * 100.0);
        }

        /// @brief Escapes a string for a JSON document.
        /// @param text The text to escape.
        /// @return The escaped text, without the surrounding quotes.
        /// @throws std::bad_alloc if the result cannot be stored.
        static std::string json_escaped(const std::string_view text) noexcept(false)
        {
            std::string out {};
            out.reserve(text.size());
            for (const auto c: text)
            {
                switch (c)
                {
                    case '"':
                        out += "\\\"";
                        break;
                    case '\\':
                        out += "\\\\";
                        break;
                    case '\n':
                        out += "\\n";
                        break;
                    case '\r':
                        out += "\\r";
                        break;
                    case '\t':
                        out += "\\t";
                        break;
                    default:
                        // Everything below a space has to be escaped; a UTF-8 continuation byte is
                        // above it and passes through, which keeps a "µs" in a note intact.
                        if (static_cast<unsigned char>(c) < 0x20u)
                            out += std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(c)));
                        else
                            out += c;

                        break;
                }
            }

            return out;
        }

        /// @brief Names an execution model for the JSON output.
        /// @param model The model.
        /// @return Its name.
        static std::string_view model_name(const execution_model model) noexcept
        {
            switch (model)
            {
                case execution_model::readiness:
                    return "readiness";
                case execution_model::completion:
                    return "completion";
                case execution_model::none:
                    break;
            }

            return "none";
        }
    } // namespace detail

    void print_comparison(const std::vector<result>& results, const registry& reg) noexcept
    {
        using namespace detail;

        if (reg.pairs().empty())
            return;

        // Only pairings with at least one side among the results, so a --filter that selected none of
        // them prints no empty section.
        std::vector<const pair_entry*> present {};
        std::size_t name_width = 8u;
        std::size_t note_width {};
        for (const auto& pair: reg.pairs())
        {
            const auto* readiness_side = side_of(results, pair.key, execution_model::readiness);
            const auto* completion_side = side_of(results, pair.key, execution_model::completion);
            if ((readiness_side == nullptr) && (completion_side == nullptr))
                continue;

            present.push_back(&pair);
            name_width = std::max(name_width, width_of(pair.key) + row_indent);

            // A skipped side explains itself in the note column, so its reason has to fit there too.
            auto note = pair.description;
            note_width = std::max(note_width, note.size());
            for (const auto* side: {readiness_side, completion_side})
                if ((side != nullptr) && side->skipped)
                    note_width = std::max(note_width, side->note.size());
        }

        if (present.empty())
            return;

        note_width = std::min(note_width, note_width_cap);

        auto header = std::format("{:<{}}", "scenario", name_width);
        add_column(header, "epoll", time_width);
        add_column(header, "io_uring", time_width);
        add_column(header, "delta", delta_width);
        add_column(header, "ops", count_width);

        const auto columns_width = width_of(header);
        const auto rule_width = (note_width == 0u) ? columns_width : (columns_width + column_gap.size() + note_width);
        if (note_width != 0u)
            add_last_column(header, "what the scenario does");

        std::println("");
        std::println("");
        std::println("epoll against io_uring - one scenario, the same work, measured on both executors");
        std::println("");
        std::println("{}", header);
        std::println("{:-<{}}", "", rule_width);

        for (const auto* pair: present)
        {
            const auto* readiness_side = side_of(results, pair->key, execution_model::readiness);
            const auto* completion_side = side_of(results, pair->key, execution_model::completion);

            // "not run" and "skipped" are different answers and the reader needs both: the first means
            // a filter or a build gate left the side out, the second that the machine could not run it.
            const auto cell = [](const result* side) noexcept(false) -> std::string
            {
                if (side == nullptr)
                    return "not run";

                return side->skipped ? std::string {"skipped"} : duration_text(quoted_ns(*side));
            };

            auto line = std::format("{:{}}{:<{}}", "", row_indent, pair->key, name_width - row_indent);
            add_column(line, cell(readiness_side), time_width);
            add_column(line, cell(completion_side), time_width);

            // A delta is only meaningful with two figures in hand, and only when both quote the same
            // kind of figure - a median against a whole-loop mean would be a number with no meaning.
            const auto both_ran =
                (readiness_side != nullptr) && (completion_side != nullptr) && !readiness_side->skipped && !completion_side->skipped;
            const auto comparable = both_ran && (readiness_side->has_distribution == completion_side->has_distribution);
            add_column(line, comparable ? delta_text(quoted_ns(*readiness_side), quoted_ns(*completion_side)) : std::string {"-"},
                       delta_width);

            // Both sides run the same amount of work by construction, so one operation count describes
            // the row; where they disagree the smaller one is the honest figure to print.
            std::size_t operations {};
            if (both_ran)
                operations = std::min(readiness_side->operations, completion_side->operations);
            else if (readiness_side != nullptr)
                operations = readiness_side->operations;
            else if (completion_side != nullptr)
                operations = completion_side->operations;

            add_column(line, (operations == 0u) ? std::string {"-"} : count_text(operations), count_width);

            // The scenario's own line normally, replaced by a skip reason when there is one to give -
            // why a side could not run is what the reader needs from that row, not what it would have done.
            auto note = pair->description;
            for (const auto* side: {readiness_side, completion_side})
                if ((side != nullptr) && side->skipped && !side->note.empty())
                    note = side->note;

            const auto note_lines = wrapped(note, note_width);
            if (!note_lines.empty())
                add_last_column(line, note_lines.front());

            std::println("{}", line);

            for (std::size_t i = 1u; i < note_lines.size(); ++i)
                std::println("{:{}}{}{}", "", columns_width, column_gap, note_lines[i]);
        }

        std::println("");
        std::println("{:-<{}}", "", rule_width);
        std::println("Each figure is the cost of one operation: the median where the case sampled every operation, the mean");
        std::println("where it timed a whole loop. delta is how the io_uring figure differs from the epoll one, so a negative");
        std::println("delta means io_uring was the faster of the two. The same figures appear in the table above, with their");
        std::println("distributions; this section only puts the two sides of each scenario on one line.");
        std::fflush(stdout);
    }

    void print_json(const std::vector<result>& results, std::FILE* const out) noexcept
    {
        using namespace detail;

        std::println(out, "{{");
        std::println(out, "  \"results\": [");
        for (std::size_t i {}; i != results.size(); ++i)
        {
            const auto& item = results[i];
            std::string line = "    {";
            line += std::format("\"name\": \"{}\"", json_escaped(item.name));
            line += std::format(", \"pair\": \"{}\"", json_escaped(item.pair_key));
            line += std::format(", \"model\": \"{}\"", model_name(item.model));
            line += std::format(", \"skipped\": {}", item.skipped ? "true" : "false");
            line += std::format(", \"operations\": {}", item.operations);
            line += std::format(", \"mean_ns\": {:.3f}", item.mean_ns);
            if (item.has_distribution)
            {
                line += std::format(", \"min_ns\": {:.3f}", item.min_ns);
                line += std::format(", \"p50_ns\": {:.3f}", item.p50_ns);
                if (item.has_p99)
                    line += std::format(", \"p99_ns\": {:.3f}", item.p99_ns);
            }

            line += std::format(", \"note\": \"{}\"", json_escaped(item.note));
            line += '}';
            if ((i + 1u) != results.size())
                line += ',';

            std::println(out, "{}", line);
        }

        std::println(out, "  ]");
        std::println(out, "}}");
        std::fflush(out);
    }

} // namespace kmx::aio::benchmark
