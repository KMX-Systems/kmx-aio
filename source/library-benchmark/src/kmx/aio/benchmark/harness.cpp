/// @file src/kmx/aio/benchmark/harness.cpp
/// @brief Benchmark harness implementation: sample statistics, and table, comparison and JSON reports.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/benchmark/harness.hpp>
#ifndef PCH
    #include <kmx/aio/benchmark/detail/driver.hpp>
    #include <kmx/aio/benchmark/detail/pair_sides.hpp>
    #include <kmx/aio/benchmark/registry.hpp>
    #include <kmx/aio/task.hpp>

    #include <algorithm>
    #include <chrono>
    #include <cmath>
    #include <coroutine>
    #include <cstddef>
    #include <cstdio>
    #include <format>
    #include <print>
    #include <string>
    #include <string_view>
    #include <vector>
#endif

namespace kmx::aio::benchmark
{
    namespace detail
    {
        [[nodiscard]] static driver make_driver(task<void> t) noexcept(false)
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
        [[nodiscard]] static std::size_t rank_index(const std::size_t count, const double fraction) noexcept
        {
            const auto rank = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(count)));
            return (rank == 0u) ? 0u : std::min(rank - 1u, count - 1u);
        }
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
        [[nodiscard]] static std::size_t width_of(const std::string_view text) noexcept
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
        [[nodiscard]] static std::string right(const std::string_view text, const std::size_t width) noexcept(false)
        {
            const auto used = width_of(text);
            std::string out((used < width) ? (width - used) : 0u, ' ');
            out += text;
            return out;
        }

        /// @brief Left-aligns text in a field, counting characters rather than bytes.
        /// @param text The text to align.
        /// @param width The field width.
        /// @return The padded text.
        /// @throws std::bad_alloc if the result cannot be stored.
        [[nodiscard]] static std::string left(const std::string_view text, const std::size_t width) noexcept(false)
        {
            const auto used = width_of(text);
            std::string out {text};
            out.append((used < width) ? (width - used) : 0u, ' ');
            return out;
        }

        /// @brief A run of one character, used for indents and horizontal rules.
        /// @param fill The character to repeat.
        /// @param width How many times to repeat it.
        /// @return The run.
        /// @throws std::bad_alloc if the result cannot be stored.
        [[nodiscard]] static std::string run_of(const char fill, const std::size_t width) noexcept(false)
        {
            return std::string(width, fill);
        }

        /// @brief Formats a figure in fixed-point notation with the given number of decimals.
        /// @details Spelled out per precision rather than as std::format("{:.{}f}", value, precision),
        ///          which does not compile under clang against libstdc++: the dynamic-width path calls
        ///          __check_dynamic_spec, which that combination leaves undefined. The three cases below
        ///          are the ones the harness actually asks for and stay inside std::format; anything
        ///          else falls through to snprintf, which formats identically and keeps the guarantee
        ///          the name makes - a caller adding a third-decimal column gets three decimals, not a
        ///          silently different notation.
        /// @param value The figure.
        /// @param precision Digits after the decimal point. Negative is read as zero, as printf does.
        /// @return The formatted figure.
        /// @throws std::bad_alloc if the result cannot be stored.
        [[nodiscard]] static std::string fixed(const double value, const int precision) noexcept(false)
        {
            switch (precision)
            {
                case 0:
                    return std::format("{:.0f}", value);
                case 1:
                    return std::format("{:.1f}", value);
                case 2:
                    return std::format("{:.2f}", value);
                default:
                    break;
            }

            const int used = (precision > 0) ? precision : 0;

            // Sized from what snprintf reports it would have written, so a large magnitude or a wide
            // precision cannot silently truncate the figure.
            const int needed = std::snprintf(nullptr, 0u, "%.*f", used, value);
            if (needed < 0)
                return std::format("{}", value);

            std::string out(static_cast<std::size_t>(needed), '\0');
            std::snprintf(out.data(), out.size() + 1u, "%.*f", used, value);
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
        [[nodiscard]] static std::string duration_text(const double ns) noexcept(false)
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
            return std::format("{} {}", fixed(value, precision), unit);
        }

        /// @brief Formats a rate with three significant digits and an SI prefix.
        /// @param per_second Operations per second.
        /// @return The formatted rate, e.g. "41.3 M/s".
        /// @throws std::bad_alloc if the result cannot be stored.
        [[nodiscard]] static std::string rate_text(const double per_second) noexcept(false)
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
            return std::format("{}{}/s", fixed(value, precision), prefix);
        }

        /// @brief Formats an operation count in groups of three digits.
        /// @param value The count.
        /// @return The grouped count, e.g. "20,000,000".
        /// @throws std::bad_alloc if the result cannot be stored.
        [[nodiscard]] static std::string count_text(const std::size_t value) noexcept(false)
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
        [[nodiscard]] static std::vector<std::string_view> wrapped(std::string_view note, const std::size_t width) noexcept(false)
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
        [[nodiscard]] static std::string_view group_of(const std::string_view name) noexcept
        {
            const auto pos = name.find('/');
            return (pos == std::string_view::npos) ? std::string_view {} : name.substr(0u, pos);
        }

        /// @brief The part of a case name after the '/', which is what the row shows.
        /// @param name The registered case name.
        /// @return The case name without its group.
        [[nodiscard]] static std::string_view case_of(const std::string_view name) noexcept
        {
            const auto pos = name.find('/');
            return (pos == std::string_view::npos) ? name : name.substr(pos + 1u);
        }
    }

    namespace detail
    {
        /// @brief The widths one report's columns need to hold every row.
        struct layout
        {
            /// @brief Width of the case-name column.
            std::size_t name {};
            /// @brief Width of the note column; zero when no row has a note.
            std::size_t note {};
            /// @brief Width of everything but the note column.
            std::size_t columns {};
            /// @brief Width of the horizontal rules, which span the note column too.
            std::size_t rule {};
        };

        /// @brief Builds the header row and measures the columns it needs.
        /// @param results The rows the report will hold.
        /// @param out Receives the widths.
        /// @return The header row.
        [[nodiscard]] std::string make_header(const std::vector<result>& results, layout& out)
        {
            out.name = 4u;
            out.note = 0u;
            for (const auto& item: results)
            {
                out.name = std::max(out.name, case_of(item.name).size() + row_indent);
                out.note = std::max(out.note, item.note.size());
            }

            out.note = std::min(out.note, note_width_cap);

            auto header = left("case", out.name);
            for (const auto* column: {"mean", "min", "p50", "p99"})
                add_column(header, column, time_width);
            add_column(header, "rate", rate_width);
            add_column(header, "ops", count_width);

            // The rule spans the note column as well, which the header itself only starts.
            out.columns = width_of(header);
            out.rule = (out.note == 0u) ? out.columns : (out.columns + column_gap.size() + out.note);
            if (out.note != 0u)
                add_last_column(header, "what it means");
            return header;
        }

        /// @brief Writes the measured columns of one row.
        /// @param line The row being built.
        /// @param item The result to write.
        void add_measurements(std::string& line, const result& item)
        {
            if (item.skipped)
            {
                // The numeric columns stay empty, so the reason lands under the note column like any
                // other remark.
                add_column(line, "skipped", time_width);
                for (int i = 0; i < 3; ++i)
                    add_column(line, "", time_width);
                add_column(line, "", rate_width);
                add_column(line, "", count_width);
                return;
            }

            add_column(line, duration_text(item.mean_ns), time_width);
            if (item.has_distribution)
            {
                add_column(line, duration_text(item.min_ns), time_width);
                add_column(line, duration_text(item.p50_ns), time_width);
                add_column(line, item.has_p99 ? duration_text(item.p99_ns) : std::string {"-"}, time_width);
            }
            else
                for (int i = 0; i < 3; ++i)
                    add_column(line, "-", time_width);

            add_column(line, rate_text((item.mean_ns > 0.0) ? (1e9 / item.mean_ns) : 0.0), rate_width);
            add_column(line, count_text(item.operations), count_width);
        }

        /// @brief Writes one row, carrying an over-long note down its own column.
        void print_row(const result& item, const layout& widths)
        {
            auto line = run_of(' ', row_indent) + left(case_of(item.name), widths.name - row_indent);
            add_measurements(line, item);

            const auto note_lines = wrapped(item.note, widths.note);
            if (!note_lines.empty())
                add_last_column(line, note_lines.front());
            std::println("{}", line);

            for (std::size_t i = 1u; i < note_lines.size(); ++i)
                std::println("{}{}{}", run_of(' ', widths.columns), column_gap, note_lines[i]);
        }

        /// @brief Returns the groups the rows fall into, in the order they are first seen.
        /// @details Collected by group rather than taken from adjacency: a paired scenario registers one
        ///          case in each of two groups, so registration order no longer keeps a group's rows
        ///          together. Rows keep their order within a group, which is what plain adjacency gave
        ///          when every group was contiguous.
        [[nodiscard]] std::vector<std::string_view> groups_of(const std::vector<result>& results)
        {
            std::vector<std::string_view> groups {};
            for (const auto& item: results)
                if (const auto group = group_of(item.name); std::find(groups.begin(), groups.end(), group) == groups.end())
                    groups.push_back(group);
            return groups;
        }

        /// @brief Writes the trailing note explaining what each column means.
        void print_legend(const layout& widths)
        {
            std::println("");
            std::println("{}", run_of('-', widths.rule));
            std::println("mean, min, p50 and p99 are the cost of one operation; rate is 1 s / mean; ops is how many were measured.");
            std::println("A \"-\" means the case timed the whole loop rather than each operation, so it has no distribution to report,");
            std::println("or - under p99 alone - that it took fewer than {} samples, too few for a percentile to name anything.",
                         min_samples_for_p99);
        }
    }

    void print_results(const std::vector<result>& results, const registry& reg) noexcept
    {
        detail::layout widths {};
        std::println("{}", detail::make_header(results, widths));
        std::println("{}", detail::run_of('-', widths.rule));

        for (const auto& group: detail::groups_of(results))
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
                if (detail::group_of(item.name) == group)
                    detail::print_row(item, widths);
        }

        detail::print_legend(widths);
        std::fflush(stdout);
    }

    namespace detail
    {
        /// @brief Field width of the delta column, wide enough for "+1234%".
        static constexpr std::size_t delta_width = 7u;

        /// @brief The figure a comparison row quotes for one side.
        /// @param item The result to read.
        /// @return The median where the case sampled each operation, the mean where it timed a whole loop.
        [[nodiscard]] static double quoted_ns(const result& item) noexcept
        {
            return item.has_distribution ? item.p50_ns : item.mean_ns;
        }

        /// @brief Finds one side of a pairing among the results.
        /// @param results The results to search.
        /// @param key The scenario name.
        /// @param model The side wanted.
        /// @return The result, or nullptr when that side did not run - it was filtered out, or the
        ///         model is not in this build.
        [[nodiscard]] static const result* side_of(const std::vector<result>& results, const std::string_view key,
                                                   const execution_model model) noexcept
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
        [[nodiscard]] static std::string delta_text(const double readiness_ns, const double completion_ns) noexcept(false)
        {
            if (readiness_ns <= 0.0)
                return "-";

            return std::format("{:+.0f}%", ((completion_ns - readiness_ns) / readiness_ns) * 100.0);
        }

        /// @brief Escapes a string for a JSON document.
        /// @param text The text to escape.
        /// @return The escaped text, without the surrounding quotes.
        /// @throws std::bad_alloc if the result cannot be stored.
        /// @brief The JSON escape one character needs, or nothing when it may pass through.
        /// @param c The character to escape.
        /// @return The replacement text, empty when @p c needs none.
        /// @note Everything below a space has to be escaped; a UTF-8 continuation byte is above it and
        ///       passes through, which keeps a "µs" in a note intact.
        [[nodiscard]] static std::string json_escape_of(const char c) noexcept(false)
        {
            switch (c)
            {
                case '"':
                    return "\\\"";
                case '\\':
                    return "\\\\";
                case '\n':
                    return "\\n";
                case '\r':
                    return "\\r";
                case '\t':
                    return "\\t";
                default:
                    break;
            }

            if (static_cast<unsigned char>(c) < 0x20u)
                return std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(c)));
            return {};
        }

        [[nodiscard]] static std::string json_escaped(const std::string_view text) noexcept(false)
        {
            std::string out {};
            out.reserve(text.size());
            for (const auto c: text)
            {
                const auto escape = json_escape_of(c);
                if (escape.empty())
                    out += c;
                else
                    out += escape;
            }

            return out;
        }

        /// @brief Names an execution model for the JSON output.
        /// @param model The model.
        /// @return Its name.
        [[nodiscard]] static std::string_view model_name(const execution_model model) noexcept
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
    }

    namespace detail
    {
        /// @brief Finds both sides of one scenario among the results.
        [[nodiscard]] pair_sides sides_of(const std::vector<result>& results, const std::string_view key)
        {
            return {side_of(results, key, execution_model::readiness), side_of(results, key, execution_model::completion)};
        }

        /// @brief Selects the pairings with at least one side present, and measures the columns.
        /// @param results The rows the report holds.
        /// @param reg The registry naming the pairings.
        /// @param out Receives the widths.
        /// @return The pairings to print; empty when a filter selected none of them.
        [[nodiscard]] std::vector<const pair_entry*> present_pairs(const std::vector<result>& results, const registry& reg, layout& out)
        {
            std::vector<const pair_entry*> present {};
            out.name = 8u;
            out.note = 0u;
            for (const auto& pair: reg.pairs())
            {
                const auto sides = sides_of(results, pair.key);
                if ((sides.readiness == nullptr) && (sides.completion == nullptr))
                    continue;

                present.push_back(&pair);
                out.name = std::max(out.name, width_of(pair.key) + row_indent);

                // A skipped side explains itself in the note column, so its reason has to fit there too.
                out.note = std::max(out.note, pair.description.size());
                for (const auto* side: {sides.readiness, sides.completion})
                    if ((side != nullptr) && side->skipped)
                        out.note = std::max(out.note, side->note.size());
            }

            out.note = std::min(out.note, note_width_cap);
            return present;
        }

        /// @brief Builds the comparison header and finishes measuring the columns.
        [[nodiscard]] std::string make_comparison_header(layout& out)
        {
            auto header = left("scenario", out.name);
            add_column(header, "epoll", time_width);
            add_column(header, "io_uring", time_width);
            add_column(header, "delta", delta_width);
            add_column(header, "ops", count_width);

            out.columns = width_of(header);
            out.rule = (out.note == 0u) ? out.columns : (out.columns + column_gap.size() + out.note);
            if (out.note != 0u)
                add_last_column(header, "what the scenario does");
            return header;
        }

        /// @brief The figure one side contributes to a comparison row.
        /// @note "not run" and "skipped" are different answers and the reader needs both: the first
        ///       means a filter or a build gate left the side out, the second that it could not run.
        [[nodiscard]] std::string comparison_cell(const result* side)
        {
            if (side == nullptr)
                return "not run";
            return side->skipped ? std::string {"skipped"} : duration_text(quoted_ns(*side));
        }

        /// @brief The operation count that describes a comparison row.
        /// @note Both sides run the same amount of work by construction; where they disagree the
        ///       smaller one is the honest figure to print.
        [[nodiscard]] std::size_t comparison_operations(const pair_sides& sides)
        {
            if (sides.both_ran())
                return std::min(sides.readiness->operations, sides.completion->operations);
            if (sides.readiness != nullptr)
                return sides.readiness->operations;
            if (sides.completion != nullptr)
                return sides.completion->operations;
            return 0u;
        }

        /// @brief The note a comparison row carries.
        /// @note The scenario's own description normally, replaced by a skip reason when there is one:
        ///       why a side could not run is what the reader needs from that row.
        [[nodiscard]] std::string_view comparison_note(const pair_entry& pair, const pair_sides& sides)
        {
            for (const auto* side: {sides.readiness, sides.completion})
                if ((side != nullptr) && side->skipped && !side->note.empty())
                    return side->note;
            return pair.description;
        }

        /// @brief Writes one scenario's row.
        void print_comparison_row(const std::vector<result>& results, const pair_entry& pair, const layout& widths)
        {
            const auto sides = sides_of(results, pair.key);
            auto line = run_of(' ', row_indent) + left(pair.key, widths.name - row_indent);
            add_column(line, comparison_cell(sides.readiness), time_width);
            add_column(line, comparison_cell(sides.completion), time_width);

            // A delta is only meaningful with two figures in hand, and only when both quote the same
            // kind of figure - a median against a whole-loop mean would be a number with no meaning.
            const auto comparable = sides.both_ran() && (sides.readiness->has_distribution == sides.completion->has_distribution);
            add_column(line, comparable ? delta_text(quoted_ns(*sides.readiness), quoted_ns(*sides.completion)) : std::string {"-"},
                       delta_width);

            const auto operations = comparison_operations(sides);
            add_column(line, (operations == 0u) ? std::string {"-"} : count_text(operations), count_width);

            const auto note_lines = wrapped(comparison_note(pair, sides), widths.note);
            if (!note_lines.empty())
                add_last_column(line, note_lines.front());
            std::println("{}", line);

            for (std::size_t i = 1u; i < note_lines.size(); ++i)
                std::println("{}{}{}", run_of(' ', widths.columns), column_gap, note_lines[i]);
        }

        /// @brief Writes the trailing note explaining the comparison columns.
        void print_comparison_legend(const layout& widths)
        {
            std::println("");
            std::println("{}", run_of('-', widths.rule));
            std::println("Each figure is the cost of one operation: the median where the case sampled every operation, the mean");
            std::println("where it timed a whole loop. delta is how the io_uring figure differs from the epoll one, so a negative");
            std::println("delta means io_uring was the faster of the two. The same figures appear in the table above, with their");
            std::println("distributions; this section only puts the two sides of each scenario on one line.");
        }
    }

    void print_comparison(const std::vector<result>& results, const registry& reg) noexcept
    {
        if (reg.pairs().empty())
            return;

        detail::layout widths {};
        const auto present = detail::present_pairs(results, reg, widths);
        if (present.empty())
            return;

        const auto header = detail::make_comparison_header(widths);
        std::println("");
        std::println("");
        std::println("epoll against io_uring - one scenario, the same work, measured on both executors");
        std::println("");
        std::println("{}", header);
        std::println("{}", detail::run_of('-', widths.rule));

        for (const auto* pair: present)
            detail::print_comparison_row(results, *pair, widths);

        detail::print_comparison_legend(widths);
        std::fflush(stdout);
    }

    /// @brief Writes one result as a JSON object.
    /// @param item The result to write.
    /// @return The object, without the separator that may follow it.
    [[nodiscard]] static std::string json_result(const result& item) noexcept(false)
    {
        std::string line = "    {";
        line += std::format("\"name\": \"{}\"", detail::json_escaped(item.name));
        line += std::format(", \"pair\": \"{}\"", detail::json_escaped(item.pair_key));
        line += std::format(", \"model\": \"{}\"", detail::model_name(item.model));
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

        line += std::format(", \"note\": \"{}\"", detail::json_escaped(item.note));
        line += '}';
        return line;
    }

    void print_json(const std::vector<result>& results, std::FILE* const out) noexcept
    {
        std::println(out, "{{");
        std::println(out, "  \"results\": [");
        for (std::size_t i {}; i != results.size(); ++i)
        {
            auto line = json_result(results[i]);
            if ((i + 1u) != results.size())
                line += ',';

            std::println(out, "{}", line);
        }

        std::println(out, "  ]");
        std::println(out, "}}");
        std::fflush(out);
    }

}
