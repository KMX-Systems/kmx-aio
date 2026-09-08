# C++ Coding Style and Conventions Guide (CSCG-2026-09)

This document outlines the coding standards and best practices for C++ projects targeting **C++11 to C++26** standards.

Adherence to these guidelines try to ensure code is correct, readable, maintainable, and highly performant.


## 1. General Principles

*   **1.1** **Readability and Maintainability:** Code must be clear, self-documenting, and easy to maintain.
    *   **1.1.1** **Operator Precedence Clarity:** Avoid expressions where operator precedence could be misunderstood without explicit parentheses (CERT EXP/CppCheck/Clang-Tidy best practice). Group sub-expressions with parentheses `()` to avoid any ambiguity regarding operator precedence. This includes:
        *   Mixing `&&` and `||` operators: use `(a && b) || (c && d)` not `a && b || c && d`.
        *   Mixing arithmetic and bitwise operators: use `(a + b) & mask` not `a + b & mask`.
        *   Ternary operators with other operators: use `(a == b) ? true_val : false_val` not `a == b ? true_val : false_val` when combining with other expressions.
        *   All sub-expressions in compound conditions should be wrapped: `if ((a == 0u) || ((b > 10u) && (c != nullptr)))` not `if (a == 0u || b > 10u && c != nullptr)`.
        *   Exception: operands that are already atomic - identifiers, literals, function calls, member accesses, subscripts - and unary expressions need no extra parentheses: use `if (!finished && item.has_value() && (count > 0u))` not `if ((!finished) && (item.has_value()) && (count > 0u))`.
    *   **1.1.2** Use brace-initialization `{}` for zero-initialization of variables. Use `()` for explicit constructor calls.
        *   **1.1.2.1** Apply `{}` to all POD types and objects that should be default-constructed: `int x {}; std::vector<int> v {}; auto p = std::make_unique<T>();`. Avoid `= 0`, `= nullptr`, or `= 0.0f`.
    *   **1.1.3** **Literal Suffixes:** Numeric literals **must** carry the suffix matching the type they are used with (`u`, `ul`, `ull`, `f`, `L`), in expressions, initializers and template arguments alike: `count > 0u`, `std::array<std::uint32_t, 3u>`, `scale * 0.5f`. This prevents signed/unsigned comparison warnings and silent narrowing conversions.

*   **1.2** **Modern C++ and Standard Library Usage:**
    *   **1.2.1** Leverage the standard library's algorithms and data structures instead of creating custom implementations for common functionalities.
    *   **1.2.2** Always use the `std::` namespace prefix for all standard library types, including fundamental types like `std::size_t`, `std::uint8_t`, and `std::int64_t`.
    *   **1.2.3** Use modern C++ features where appropriate (e.g., `if constexpr`, `std::format`, `std::span`, spaceship operator `<=>`) to improve type safety, performance, and clarity.

*   **1.3** **Minimalism and Expressiveness:** Strive for concise code that leverages C++ features to eliminate boilerplate and clearly express intent. Minimalism should enhance readability, not obscure it with overly clever or terse syntax.
    *   **1.3.1** Use uniform initialization `{}` to return default-constructed or empty-state objects where applicable. This is more concise and expressive than alternatives. For example, prefer `return {};` over `return std::nullopt;` or `return std::vector<T>();`.
    *   **1.3.2** Use `= default` and `= delete` to explicitly control the generation of special member functions, letting the compiler provide the implementation where appropriate.
    *   **1.3.3** Avoid redundant abstractions. Do not create a complex class hierarchy when a simple `struct` or a free function will suffice.
    *   **1.3.4** **Nested Template Types:** A type spelled with two or more nested template instantiations **must** be replaced by a meaningful type alias named after the concept it represents, not after its structure. Declare the alias next to the type or interface it belongs to and use it consistently in signatures, members, and local variables.
        ```cpp
        using datagram_result_t = std::expected<datagram, std::error_code>;
        using datagram_task_t = task<datagram_result_t>;

        datagram_task_t receive();                              // Correct
        task<std::expected<datagram, std::error_code>> receive(); // Incorrect
        ```

*   **1.4** **Correctness and Robustness:** Prioritize code that is verifiably correct.
    *   **1.4.1** **Const Correctness:** All variables, parameters, and member functions that are not intended to be modified **must** be declared `const`.
    *   **1.4.2** **Exception Safety:** All functions **must** have a correct `noexcept` specifier. Functions that can fail due to invalid input or runtime conditions throw exceptions and are marked `noexcept(false)`. Functions that handle errors via other means (e.g., return codes) and are guaranteed not to throw are marked `noexcept`.

## 2. Naming Conventions

*   **2.1** A strict `snake_case` convention is enforced for all identifiers, with the exception of template parameters.

| Entity Type | Convention | Example |
| :--- | :--- | :--- |
| **Namespaces** | `lowercase_with_underscores` | `project_name::data_model` |
| **Classes & Structs** | `lowercase_with_underscores` | `widget_manager`, `coordinate` |
| **Functions & Methods** | `lowercase_with_underscores` | `calculate_total_value`, `parse_input` |
| **Variables & Data Members**| `lowercase_with_underscores` | `item_count`, `is_valid` |
| **Private Data Members** | `lowercase_with_underscores_` (trailing underscore) | `buffer_`, `current_index_` |
| **Constants (`constexpr`)**| `lowercase_with_underscores` | `max_iterations`, `default_tolerance` |
| **Type Aliases (`using`)**| `lowercase_with_underscores_t` | `value_t`, `id_t` |
| **Template Parameters** | `PascalCase` | `template <typename T, typename Allocator>` |

## 3. Formatting and Layout

*   **3.1** **Indentation:** Use **4 spaces** for each indentation level. The use of tabs is forbidden.

*   **3.2** **Brace Style:** Use the **Allman brace style**, where the opening brace `{` is placed on a new, aligned line. Braces are **exclusively** used for scopes containing multiple statements. For single-statement blocks within control structures (`if`, `while`, `for`, `else`, `do`), braces **must be omitted**. Keep the code compact by using directly the statement with a padding (indentation).
    *   **3.2.1** Single-statement blocks include simple return, assignment, function call, or conditional within a loop. Examples:
        ```cpp
        if (condition)
            return result;  // Correct: no braces

        for (auto item : collection)
            if (item != nullptr)  // Correct: nested single-statement, no outer braces
                process(item);

        if (condition)
        {
            statement1();
            statement2();  // Correct: multiple statements, braces required
        }
        ```

*   **3.3** **Spacing:**
    *   **3.3.1** Use a single space around binary and ternary operators.
    *   **3.3.2** Do not place a space between a function name and its opening parenthesis.
    *   **3.3.3** Place a single space after control-flow keywords (`if`, `for`, `while`).
    *   **3.3.4** Do not place a space after a cast.

*   **3.4** **Header Organization:**
    *   **3.4.1** Use `#pragma once` for include guards.
    *   **3.4.2** Use `#ifndef PCH` guards to support optional precompiled headers.
    *   **3.4.3** Organize includes logically: project-specific headers, then standard library headers.
    *   **3.4.4** Use forward declarations where possible to minimize header dependencies.

## 4. API and Language Design

*   **4.1** **Error Handling:**
    *   **4.1.1** **Exceptions (`noexcept(false)`):** Use for reporting precondition violations (e.g., invalid arguments) and unrecoverable runtime errors (e.g., convergence failure, out-of-range values).
    *   **4.1.2** **Return Codes (`noexcept`):** Use for performance-critical functions where failure is a predictable and frequent outcome. An `enum class` should define the possible error states.
    *   **4.1.3** **Custom Exceptions:** Prefer project-defined exception types over standard library exceptions, even when a type alias is used to shorten the name. An alias must refer to a custom exception type, not to `std::exception` or another standard exception. Domain-specific exceptions make error semantics explicit and avoid ambiguous catch sites.

*   **4.2** **Data Structures:**
    *   **4.2.1** Use `struct` for simple aggregate data types (Plain Old Data).
    *   **4.2.2** Use `class` when invariants must be maintained through a public interface with private data members.
    *   **4.2.3** **Enumerations over Strings:** A value drawn from a fixed, known set - a state, kind, mode, counter or option identifier - **must** be an `enum class`, never a string. Strings defeat compile-time checking, make typos silent runtime failures, force dispatch through chains of comparisons, and prevent exhaustiveness diagnostics.
        ```cpp
        void add(counter_t::id, amount);          // Correct: switch dispatches on the enum
        void add(std::string_view name, amount);  // Incorrect: string compared against every candidate
        ```
        *   **4.2.3.1** Convert at the boundary. Where a string is unavoidable (parsing, configuration files, external protocols), map it to the enumeration once at the point of entry and use only the enumeration internally.

*   **4.3** **Namespaces:**
    *   **4.3.1** **Hierarchy:** Structure namespaces hierarchically from general to specific concepts (e.g., `kmx::gis::coordinate::wgs84`).
    *   **4.3.2** **Unique Naming:** Words within a namespace hierarchy should be unique.
    *   **4.3.3** **Anonymous Namespaces:** The use of anonymous namespaces is forbidden. Prefer `static` functions in `.cpp` files for translation-unit-local functions, and use a nested `detail` or `internal` namespace for other internal-linkage entities.
        *   **4.3.3.1** All functions and types in the `detail` namespace **must** be qualified at their call sites or referenced via the full path: `detail::write_be16(...)`, `detail::helper_function()`. Do not use unqualified lookup or `using namespace detail;`.
    *   **4.3.4** **Inline Namespaces:** Use `inline` namespaces for versioning or to export a specific set of functionality from a nested implementation namespace, making it part of the parent's interface.

*   **4.4** **`std::hash` Specialization:** Provide a specialization of `std::hash` for any custom type intended to be used as a key in an unordered associative container.

*   **4.5** **Functions and Methods:**
    *   **4.5.1** Non-template methods and functions longer than 2 lines of code **must** be defined in `.cpp` files.
    *   **4.5.2** Prefer `static` functions in `.cpp` files rather than using anonymous namespaces for them.
    *   **4.5.3** Template methods longer than 12 lines of code **should** be defined outside classes.
    *   **4.5.4** `constexpr` and `consteval` functions and methods do not need the `inline` specifier.
    *   **4.5.5** **Branch Prediction Hints (C++20):** Use `[[likely]]`/`[[unlikely]]` only on hot-path branches whose outcome is strongly skewed and known (error paths, loop exits); leave balanced or unknown branches unannotated. They are hints only - correctness **must never** depend on them.
        *   **4.5.5.1** The attribute applies to the branch **statement**, and precedes a `case` label: `if (buffer.empty()) [[unlikely]] return error_t::no_data;`, `[[likely]] case state_t::running:`.
    *   **4.5.6** **Lambdas:** A lambda body longer than 3 lines **must** be extracted into a named function or method, leaving the lambda as glue that only forwards to it: `[this](const auto& item) { return process(item); }`.

## 5. Documentation

*   **5.1** **API Documentation:** All public declarations (namespaces, types, functions, and public members) **must** be documented using Doxygen.
    *   **5.1.1** Use triple-slash (`///`) comments for Doxygen blocks.
    *   **5.1.2** Use Doxygen's `@` commands for structured documentation.

*   **5.2** **Required Doxygen Tags:**
    *   `@brief`: A concise one-line summary.
    *   `@param`: For every function parameter.
    *   `@tparam`: For every template parameter.
    *   `@return`: Description of the return value.
    *   `@throws`: For each type of exception a function can throw.
    *   `@note`, `@warning`: For important remarks or potential issues.
    *   `@reference`: To cite external standards, documents, or sources.

*   **5.3** **Implementation Comments:** Use standard `//` comments within implementation files to clarify complex algorithms, non-obvious logic, or the purpose of "magic" constants. Comments should explain the *why*, not re-state the *what*.

---
Copyright © 2025 - present KMX Systems. All rights reserved.
