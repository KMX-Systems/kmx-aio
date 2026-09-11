/// @file inc/kmx/aio/benchmark/harness.hpp
/// @brief Minimal timing harness shared by the kmx-aio micro-benchmarks.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/task.hpp>

    #include <chrono>
    #include <cstddef>
    #include <cstdint>
    #include <cstdio>
    #include <string>
    #include <string_view>
    #include <vector>
#endif

namespace kmx::aio::benchmark
{
    /// @brief Clock used for every measurement in this harness.
    using clock_t = std::chrono::steady_clock;

    /// @brief Which execution model a case ran on.
    /// @details Only the two paired sides are named. A case measuring something with no executor in
    ///          it - a codec, the heap, a raw system call - is `none` and takes no part in the
    ///          side-by-side report.
    enum class execution_model : std::uint8_t
    {
        none,       ///< No executor, or not comparable between the two.
        readiness,  ///< The epoll executor.
        completion, ///< The io_uring executor.
    };

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

        /// @brief The scenario this case is one side of, or empty when it stands alone.
        /// @details Copied from the registry after the case runs, rather than set by the case itself:
        ///          a case function measures, and does not need to know it is being compared.
        std::string_view pair_key {};

        /// @brief Which side of that scenario this is.
        execution_model model {};
    };

    class registry;

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

    /// @brief Attaches a case's explanatory note, unless the case was skipped.
    /// @details A skipped result already carries the reason it could not run, and that reason is what
    ///          the reader needs from that row. Assigning result::note directly after a call that may
    ///          have skipped overwrites it with a description of work that never happened.
    /// @param outcome The result to annotate.
    /// @param note The line to print beside the figures.
    /// @return The annotated result.
    /// @throws std::bad_alloc if the note cannot be stored.
    [[nodiscard]] result with_note(result outcome, std::string note) noexcept(false);

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

    /// @brief Prints the epoll-against-io_uring section: one row per paired scenario.
    /// @details Reads the figures out of the results the main table already printed, so the two
    ///          sections can never disagree. A scenario with only one side present prints the figure
    ///          it has and why the other is missing.
    /// @param results The results to read, in registration order.
    /// @param reg The registry the pairings came from.
    void print_comparison(const std::vector<result>& results, const registry& reg) noexcept;

    /// @brief Writes every result as one JSON document.
    /// @details The whole feature matrix cannot be linked into one binary - QUIC pulls in BoringSSL
    ///          and OPC UA pulls in OpenSSL - so a complete comparison is always assembled from
    ///          several runs. This is the form those runs are merged from.
    /// @param results The results to emit, in registration order.
    /// @param out Where to write. The library logs to stdout as it starts an executor, so a document
    ///            written there has log lines through it; give this a file to get parseable output.
    void print_json(const std::vector<result>& results, std::FILE* out) noexcept;

    /// @brief Keeps the optimizer from discarding a computed value.
    /// @tparam T The value type.
    /// @param value The value to keep.
    template <typename T>
    void keep(T&& value) noexcept
    {
        asm volatile("" : : "r,m"(value) : "memory");
    }

}
