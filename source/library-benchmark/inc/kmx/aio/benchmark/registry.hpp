/// @file inc/kmx/aio/benchmark/registry.hpp
/// @brief The registry of benchmark cases, the scenarios they pair into and the groups they report under.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/benchmark/harness.hpp>

    #include <string_view>
    #include <vector>
#endif

namespace kmx::aio::benchmark
{
    /// @brief Signature of a benchmark case.
    /// @param scale Multiplier applied to the case's own iteration count.
    /// @return The measured result.
    using case_fn_t = result (*)(double scale);

    /// @brief A registered benchmark case.
    struct case_entry
    {
        std::string_view name; ///< Case name used for reporting and filtering.
        case_fn_t run;         ///< The function performing the measurement.

        /// @brief The scenario this case is one side of, or empty when it stands alone.
        std::string_view pair_key {};

        /// @brief Which execution model the case drives.
        execution_model model {};
    };

    /// @brief One scenario measured on both execution models.
    struct pair_entry
    {
        std::string_view key;         ///< The scenario name, as it heads its row in the comparison.
        std::string_view description; ///< One line saying what the scenario does.
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
        /// @brief Registers one case that stands on its own.
        /// @param name The case name.
        /// @param run The measuring function.
        /// @throws std::bad_alloc if the case list cannot grow.
        void add(std::string_view name, case_fn_t run) noexcept(false);

        /// @brief Registers one side of a scenario measured on both execution models.
        /// @details The two sides register themselves independently, from the translation unit that is
        ///          gated on their own model. A build with only one model therefore still gets that
        ///          model's case - it simply has nothing to compare it against, and the report says
        ///          so. The case is an ordinary one in every other respect: it appears in its own
        ///          group in the main table under the name given here.
        /// @param key The scenario name, shared with the other side.
        /// @param model Which side this is.
        /// @param name The case name.
        /// @param run The measuring function.
        /// @throws std::bad_alloc if the case or pair list cannot grow.
        void add_paired(std::string_view key, execution_model model, std::string_view name, case_fn_t run) noexcept(false);

        /// @brief Records what a scenario does, for the heading of its comparison row.
        /// @details Kept apart from add_paired because neither side owns the description - it has to
        ///          read as one sentence about work both of them do.
        /// @param key The scenario name.
        /// @param description One line saying what the scenario does.
        /// @throws std::bad_alloc if the pair list cannot grow.
        void describe_pair(std::string_view key, std::string_view description) noexcept(false);

        /// @brief Records what a group of cases is for, so the report can head the group with it.
        /// @param group The group name, the part of a case name before the '/'.
        /// @param description One line saying what the group measures.
        /// @throws std::bad_alloc if the group list cannot grow.
        void describe(std::string_view group, std::string_view description) noexcept(false);

        /// @brief Returns every registered case.
        /// @return The registered cases, in registration order.
        [[nodiscard]] const std::vector<case_entry>& cases() const noexcept { return cases_; }

        /// @brief Returns every registered pairing.
        /// @return The pairings, in registration order.
        [[nodiscard]] const std::vector<pair_entry>& pairs() const noexcept { return pairs_; }

        /// @brief Returns the description recorded for a group.
        /// @param group The group name.
        /// @return The description, or an empty view when the group was never described.
        [[nodiscard]] std::string_view description(std::string_view group) const noexcept;

        /// @brief Returns the description recorded for a pairing.
        /// @param key The scenario name.
        /// @return The description, or an empty view when the key names no pairing.
        [[nodiscard]] std::string_view pair_description(std::string_view key) const noexcept;

    private:
        /// @brief The registered cases.
        std::vector<case_entry> cases_;

        /// @brief The described groups.
        std::vector<group_entry> groups_;

        /// @brief The registered pairings.
        std::vector<pair_entry> pairs_;
    };
}
