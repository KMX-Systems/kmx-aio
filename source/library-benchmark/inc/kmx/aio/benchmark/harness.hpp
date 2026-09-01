/// @file aio/benchmark/harness.hpp
/// @brief Minimal timing harness shared by the kmx-aio micro-benchmarks.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <chrono>
    #include <cstddef>
    #include <string>
    #include <string_view>
    #include <vector>

    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::benchmark
{
    /// @brief Clock used for every measurement in this harness.
    using clock_t = std::chrono::steady_clock;

    /// @brief One benchmark outcome, in nanoseconds per operation.
    struct result
    {
        std::string name;          ///< Case name, as registered.
        std::string note;          ///< Free-form remark, e.g. why a case was skipped.
        std::size_t operations {}; ///< Number of operations measured.
        double mean_ns {};         ///< Mean cost of one operation.
        double min_ns {};          ///< Fastest observed operation (samples only).
        double p50_ns {};          ///< Median operation (samples only).
        double p99_ns {};          ///< 99th percentile operation (samples only).
        bool has_distribution {};  ///< True when the min and p50 fields are populated.
        bool has_p99 {};           ///< True when there were enough samples for a 99th percentile to mean anything.
        bool skipped {};           ///< True when the case could not run on this machine.
    };

    /// @brief Signature of a benchmark case.
    /// @param scale Multiplier applied to the case's own iteration count.
    /// @return The measured result.
    using case_fn_t = result (*)(double scale);

    /// @brief A registered benchmark case.
    struct case_entry
    {
        std::string_view name; ///< Case name used for reporting and filtering.
        case_fn_t run;         ///< The function performing the measurement.
    };

    /// @brief A benchmark group and what it covers.
    struct group_entry
    {
        std::string_view name;        ///< Group name, the part of a case name before the '/'.
        std::string_view description; ///< One line saying what the group is for, printed as its heading.
    };

    /// @brief Collects the benchmark cases the executable knows about.
    class registry
    {
    public:
        /// @brief Registers one case.
        /// @param name The case name.
        /// @param run The measuring function.
        /// @throws std::bad_alloc if the case list cannot grow.
        void add(std::string_view name, case_fn_t run) noexcept(false);

        /// @brief Records what a group of cases is for, so the report can head the group with it.
        /// @param group The group name, the part of a case name before the '/'.
        /// @param description One line saying what the group measures.
        /// @throws std::bad_alloc if the group list cannot grow.
        void describe(std::string_view group, std::string_view description) noexcept(false);

        /// @brief Returns every registered case.
        /// @return The registered cases, in registration order.
        [[nodiscard]] const std::vector<case_entry>& cases() const noexcept { return cases_; }

        /// @brief Returns the description recorded for a group.
        /// @param group The group name.
        /// @return The description, or an empty view when the group was never described.
        [[nodiscard]] std::string_view description(std::string_view group) const noexcept;

    private:
        /// @brief The registered cases.
        std::vector<case_entry> cases_;

        /// @brief The described groups.
        std::vector<group_entry> groups_;
    };

    /// @brief Scales an iteration count, never returning zero.
    /// @param base The nominal iteration count.
    /// @param scale The multiplier requested on the command line.
    /// @return The scaled iteration count, at least one.
    [[nodiscard]] std::size_t scaled(std::size_t base, double scale) noexcept;

    /// @brief Builds a result from a total elapsed time.
    /// @param name The case name.
    /// @param operations The number of operations performed.
    /// @param total The time the whole run took.
    /// @return The mean-only result.
    /// @throws std::bad_alloc if the name cannot be stored.
    [[nodiscard]] result from_total(std::string name, std::size_t operations, clock_t::duration total) noexcept(false);

    /// @brief Builds a result from per-operation samples.
    /// @param name The case name.
    /// @param samples_ns The per-operation durations, in nanoseconds. Sorted in place.
    /// @return The result including percentiles, by nearest rank. The 99th is left unreported below
    ///         a hundred samples, where it would name one of the few slowest and not a percentile.
    /// @throws std::bad_alloc if the name cannot be stored.
    [[nodiscard]] result from_samples(std::string name, std::vector<double>& samples_ns) noexcept(false);

    /// @brief Builds a result for a case that could not run here.
    /// @param name The case name.
    /// @param reason Why the case was skipped.
    /// @return A skipped result.
    /// @throws std::bad_alloc if the strings cannot be stored.
    [[nodiscard]] result skipped(std::string name, std::string reason) noexcept(false);

    /// @brief Drives a task to completion on the calling thread.
    /// @param t The task to run. It must not suspend on anything the caller has to complete.
    void run_sync(task<void>&& t) noexcept;

    /// @brief Prints the results as an aligned table, one section per group.
    /// @param results The results to print, in registration order.
    /// @param reg The registry the cases came from, for the group headings.
    void print_results(const std::vector<result>& results, const registry& reg) noexcept;

    /// @brief Keeps the optimizer from discarding a computed value.
    /// @tparam T The value type.
    /// @param value The value to keep.
    template <typename T>
    void keep(T&& value) noexcept
    {
        asm volatile("" : : "r,m"(value) : "memory");
    }

} // namespace kmx::aio::benchmark
