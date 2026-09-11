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
    *   **1.2.4** **No C-style Casts:** C-style casts `(T)value` are forbidden. Use the named casts `static_cast`, `const_cast` and `reinterpret_cast`, or `std::bit_cast`, so every conversion states what it does and can be found by searching.

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
    *   **1.3.5** **Alias Templates for Dependent Types:** A long template type that depends on template parameters and is used in parameter declarations **must** be replaced by an alias template. The alias fixes the arguments that do not vary and takes only the dependent ones as its own template parameters, so each parameter spells out just the part that changes. Name the alias after the concept it represents and declare it next to the interface it belongs to.
        ```cpp
        template <typename Unit>
        using world_quantity_t = quantity3<math::frame::id::world, Unit>;

        // Correct
        template <core::length_unit Length, core::speed_unit Speed, core::duration_unit Duration>
        state(const world_quantity_t<Length>& position, const world_quantity_t<Speed>& velocity, const Duration valid_at) noexcept(false);

        // Incorrect
        template <core::length_unit Length, core::speed_unit Speed, core::duration_unit Duration>
        state(const math::vector3::quantity3<math::frame::id::world, Length>& position,
              const math::vector3::quantity3<math::frame::id::world, Speed>& velocity, const Duration valid_at) noexcept(false);
        ```

*   **1.4** **Correctness and Robustness:** Prioritize code that is verifiably correct.
    *   **1.4.1** **Const Correctness:** All variables, parameters, and member functions that are not intended to be modified **must** be declared `const`.
    *   **1.4.2** **Exception Safety:** All functions **must** have a correct `noexcept` specifier. Functions that can fail due to invalid input or runtime conditions throw exceptions and are marked `noexcept(false)`. Functions that handle errors via other means (e.g., return codes) and are guaranteed not to throw are marked `noexcept`.

## 2. Naming Conventions

*   **2.1** A strict `snake_case` convention is enforced for all identifiers, with the exception of template parameters and macros.

| Entity Type | Convention | Example |
| :--- | :--- | :--- |
| **Namespaces** | `lowercase_with_underscores` | `project_name::data_model` |
| **Classes & Structs** | `lowercase_with_underscores` | `wind_segment`, `coordinate` |
| **Functions & Methods** | `lowercase_with_underscores` | `calculate_total_value`, `parse_input` |
| **Variables & Data Members**| `lowercase_with_underscores` | `item_count`, `is_valid` |
| **Private Data Members** | `lowercase_with_underscores_` (trailing underscore) | `buffer_`, `current_index_` |
| **Constants (`constexpr`)**| `lowercase_with_underscores` | `max_iterations`, `default_tolerance` |
| **Type Aliases (`using`)**| `lowercase_with_underscores_t` | `value_t`, `id_t` |
| **Template Parameters** | `PascalCase` | `template <typename T, typename Allocator>` |
| **Macros** | `KMX_<PROJECT>_UPPER_CASE` | `KMX_UNIT_LITERALS` |

*   **2.2** **File Names:** File names use `snake_case`, with the `.hpp` extension for headers and `.cpp` for sources.
    *   **2.2.1** **Test Files:** A test source mirrors the path of the code it tests beneath its test component's `src/` and adds the `_test` suffix: `library-test/src/kmx/sat/cdcl/controller/restart_test.cpp` tests `library/src/kmx/sat/cdcl/controller/restart.cpp`.

## 3. Formatting and Layout

*   **3.1** **Indentation:** Use **4 spaces** for each indentation level. The use of tabs is forbidden.
    *   **3.1.1** The body of a namespace is indented one level.
    *   **3.1.2** Access specifiers (`public:`, `protected:`, `private:`) are aligned with the `class` or `struct` keyword.
    *   **3.1.3** `case` labels are indented one level inside their `switch`, and their statements one level further.
        ```cpp
        namespace kmx::sling::core
        {
            class engine
            {
            public:
                void run() noexcept;

            private:
                mode_t mode_ {};
            };

            void engine::run() noexcept
            {
                switch (mode_)
                {
                    case mode_t::fast:
                        return run_fast();
                    default:
                        return run_safe();
                }
            }
        }
        ```

*   **3.2** **Brace Style:** Use the **Allman brace style**, where the opening brace `{` is placed on a new, aligned line. Braces are **exclusively** used for scopes containing multiple statements. For single-statement blocks within control structures (`if`, `while`, `for`, `else`, `do`), braces **must be omitted**. Keep the code compact by using directly the statement with a padding (indentation).
    *   **3.2.1** Single-statement blocks include simple return, assignment, function call, or conditional within a loop. Examples:
        ```cpp
        if (condition)
            return result;  // Correct: no braces

        for (auto item: collection)
            if (item != nullptr)  // Correct: nested single-statement, no outer braces
                process(item);

        if (condition)
        {
            statement1();
            statement2();  // Correct: multiple statements, braces required
        }
        ```
    *   **3.2.2** **Namespace Closing Braces:** The closing brace of a namespace carries no trailing comment: `}`, not `} // namespace kmx::sling`.

*   **3.3** **Spacing:**
    *   **3.3.1** Use a single space around binary and ternary operators.
    *   **3.3.2** Do not place a space between a function name and its opening parenthesis.
    *   **3.3.3** Place a single space after control-flow keywords (`if`, `for`, `while`).
    *   **3.3.4** Do not place a space after a cast.
    *   **3.3.5** **Empty Line After Blocks:** A control-flow statement (`if`, `else`, `for`, `while`, `do`-`while`, `switch`) **must** be followed by an empty line only if its body is braced. The empty line visually closes the block, so the code that follows is not read as part of it.
        ```cpp
        // Correct: empty line after the braced block, none after the unbraced bodies
        if (!decoded.has_value())
        {
            // A wrapper too short to read never reaches the session, but is counted as one that did not authenticate.
            if (announces_wrapper(wire))
                session_.note_unauthenticated();
            return std::optional<std::size_t> {};
        }

        if (const auto* const wrapper = std::get_if<secure_wrapper_frame>(&decoded->payload))
        {
            const auto opened = session_.open(*wrapper, plain, now);
            if (!opened.has_value())
                return ends_session(opened.error()) ? unwrapped_t {std::unexpected(opened.error())} : std::optional<std::size_t> {};
            return opened->for_tunnel ? std::optional<std::size_t> {opened->size} : std::optional<std::size_t> {};
        }

        // Incorrect: no empty line after the first braced block
        if (!decoded.has_value())
        {
            // A wrapper too short to read never reaches the session, but is counted as one that did not authenticate.
            if (announces_wrapper(wire))
                session_.note_unauthenticated();
            return std::optional<std::size_t> {};
        }
        if (const auto* const wrapper = std::get_if<secure_wrapper_frame>(&decoded->payload))
        {
            const auto opened = session_.open(*wrapper, plain, now);
            if (!opened.has_value())
                return ends_session(opened.error()) ? unwrapped_t {std::unexpected(opened.error())} : std::optional<std::size_t> {};
            return opened->for_tunnel ? std::optional<std::size_t> {opened->size} : std::optional<std::size_t> {};
        }
        ```
        *   **3.3.5.1** The empty line follows the complete statement: after the last branch of an `if`/`else` chain, after the `while (condition);` of a `do`-`while`, and after the outer statement when one is nested as the unbraced body of another. It is omitted when the statement is the last one in its enclosing scope, directly before the closing brace `}`.
    *   **3.3.6** **Consecutive Empty Lines:** Never place more than one empty line in a row.
    *   **3.3.7** **Pointers and References:** `*` and `&` attach to the type, not to the name: `const widget& item`, `const char* name`.
    *   **3.3.8** **Braced Initializers:** Place a single space between a type or a name and its braced initializer: `std::size_t count {};`, `return unwrapped_t {value};`.
    *   **3.3.9** **Range-based `for`:** Place no space before the colon and a single space after it: `for (const auto& item: items)`.
    *   **3.3.10** **Templates:** Place a single space after the `template` keyword, and put the template parameter list on its own line, above the declaration it introduces.
        ```cpp
        template <typename Visitor>
        void visit(const Visitor& visitor) const;
        ```

*   **3.4** **Header Organization:**
    *   **3.4.1** Use `#pragma once` for include guards.
    *   **3.4.2** Use `#ifndef PCH` guards to support optional precompiled headers. The includes inside the guard are indented one level.
    *   **3.4.3** **Include Order:** Order includes from the most specific to the most general, in the groups below. Separate the groups with an empty line and sort each group alphabetically.
        *   The file's own header, first in a `.cpp` file and outside the `#ifndef PCH` guard.
        *   Project headers (`<kmx/...>`).
        *   Third-party library headers (`<openssl/...>`, `<catch2/...>`, `<QString>`).
        *   System headers: the C++ standard library first, then C and operating system headers (`<sys/...>`, `<unistd.h>`).

        A header that forgets one of its own includes then fails to compile in the first file that includes it, instead of silently relying on an include placed above it. This matters most for header-only code, which has no `.cpp` file to include it first.
        ```cpp
        #include <kmx/aio/tcp/stream.hpp>
        #ifndef PCH
            #include <kmx/aio/error_code.hpp>
            #include <kmx/aio/task.hpp>

            #include <openssl/ssl.h>

            #include <cstdint>
            #include <span>
            #include <sys/socket.h>
        #endif
        ```
    *   **3.4.4** Use forward declarations where possible to minimize header dependencies.
    *   **3.4.5** **One Class per Header:** A header **must** define at most one class, and **must** be named after that class: `widget::manager` is defined in `widget/manager.hpp`. A `struct` with any method that is not defaulted (`= default`) counts as a class under this rule. Plain structs, with only data members and defaulted methods, have no such limit: several may share a header, on their own or next to the class that uses them. Types nested inside a class are part of that class. A header named after its class declares the namespace of its directory (see 4.3.5.1), since a namespace named after the class would repeat its name (see 4.3.2.1).
        ```text
        api/kmx/gui/widget/item.hpp       // Correct: class item
        api/kmx/gui/widget/manager.hpp    // Correct: class manager, plus the plain struct create_params it takes
        api/kmx/gui/widget/geometry.hpp   // Correct: plain structs position and size

        api/kmx/gui/widget/widget.hpp     // Incorrect: classes item and manager, which need item.hpp and manager.hpp
        api/kmx/gui/widget/registry.hpp   // Incorrect: class manager in a header not named after it
        api/kmx/gui/widget/geometry.hpp   // Incorrect: struct size has the method area, so it belongs in size.hpp
        ```
    *   **3.4.6** **No Classes in Source Files:** A class, including a `struct` with a method that is not defaulted (see 3.4.5), **must not** be defined in a `.cpp` file. A class defined in a `.cpp` file is visible only inside that file, so a test cannot include it and can reach it only through the code that uses it. Define the class in its own header instead; its methods still follow 4.5.1, so the longer ones are implemented in the matching `.cpp` file. This includes internal helper classes, whose headers go under `inc/`. Plain structs may still be defined in a `.cpp` file. Test sources (see 2.2.1) are exempt, since their fixtures and test doubles are not under test themselves.
        ```text
        inc/kmx/gui/widget/detail/cache.hpp   // Correct: class cache, a helper used by manager.cpp
        src/kmx/gui/widget/detail/cache.cpp   // Correct: the methods of class cache
        src/kmx/gui/widget/manager.cpp        // Correct: plain struct entry, used only in this file

        src/kmx/gui/widget/manager.cpp        // Incorrect: class cache, which no test can include
        ```

*   **3.5** **Line Length:** A line of code **must not** exceed **140 characters**, counting indentation and trailing comments. A statement, signature or expression that would exceed the limit **must** be wrapped across multiple lines.

*   **3.6** **Formatter:** Format code with the shared `.clang-format` distributed with this guide. It enforces the layout rules of this section except two, which must be kept by hand: the empty line after a braced block (3.3.5), and brace removal on the single-statement branches of an `if`/`else` chain in which another branch needs braces (3.2).

## 4. API and Language Design

*   **4.1** **Error Handling:**
    *   **4.1.1** **Exceptions (`noexcept(false)`):** Use for reporting precondition violations (e.g., invalid arguments) and unrecoverable runtime errors (e.g., convergence failure, out-of-range values).
    *   **4.1.2** **Expected Results (`noexcept`, C++23):** Use for performance-critical functions where failure is a predictable and frequent outcome. Return `std::expected<T, E>`, where `E` is `std::error_code` or an `enum class` defining the possible error states, and mark the function `[[nodiscard]]` (see 4.5.9).
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
    *   **4.2.4** **Enumeration Underlying Type:** An `enum class` **must** declare its underlying type: `enum class termination_reason : std::uint8_t`. An unscoped `enum` is allowed only where an external API requires implicit conversion to an integer, such as a C interface or Qt model roles.
    *   **4.2.5** **Explicit Constructors:** A constructor callable with a single argument, other than a copy or move constructor, **must** be `explicit`, so a value never converts silently into the class type.

*   **4.3** **Namespaces:**
    *   **4.3.1** **Hierarchy:** Structure namespaces hierarchically from general to specific concepts (e.g., `kmx::gis::coordinate::wgs84`).
    *   **4.3.2** **Unique Naming:** Words within a namespace hierarchy should be unique.
        *   **4.3.2.1** **No Namespace Repetition:** This uniqueness extends to the entities a namespace declares. The names of types, functions, variables, constants and type aliases **must not** repeat any word of their enclosing namespace path, whether from the innermost namespace or from any outer one. The namespace already provides that context, so repeating it only makes names longer and qualified uses stutter (`normalized::normalized_atmosphere`). If a name is ambiguous where it is used, qualify it with its namespace there instead of building the namespace into the name.
            ```cpp
            namespace kmx::sling::domain::normalized
            {
                struct wind_segment;            // Correct: used as normalized::wind_segment
                struct atmosphere;              // Correct
                struct constraints;             // Correct
            }

            namespace kmx::sling::domain::normalized
            {
                struct normalized_wind_segment; // Incorrect: repeats "normalized"
                struct normalized_atmosphere;   // Incorrect: repeats "normalized"
                struct domain_constraints;      // Incorrect: repeats "domain" from an outer namespace
            }
            ```
    *   **4.3.3** **Anonymous Namespaces:** The use of anonymous namespaces is forbidden. Prefer `static` functions in `.cpp` files for translation-unit-local functions, and use a nested `detail` or `internal` namespace for other internal-linkage entities.
        *   **4.3.3.1** All functions and types in the `detail` namespace **must** be qualified at their call sites or referenced via the full path: `detail::write_be16(...)`, `detail::helper_function()`. Do not use unqualified lookup or `using namespace detail;`.
    *   **4.3.4** **Inline Namespaces:** Use `inline` namespaces for versioning or to export a specific set of functionality from a nested implementation namespace, making it part of the parent's interface.
    *   **4.3.5** **Directory Structure:** A file's directory path **must** mirror its namespace path, one directory per namespace level, starting at `kmx/` beneath the source root (`api/`, `inc/` or `src/`). An include path then names the namespace, and a namespace names the directory where its files live.
        ```text
        api/kmx/sling/domain/drag/drag.hpp              // Correct: namespace kmx::sling::domain::drag
        api/kmx/sling/domain/drag/point.hpp             // Correct: namespace kmx::sling::domain::drag::point
        inc/kmx/sling/core/detail/result_access.hpp     // Correct: namespace kmx::sling::core::detail
        src/kmx/sling/domain/normalized.cpp             // Correct: namespace kmx::sling::domain::normalized

        api/kmx/sling/domain/drag/point.hpp             // Incorrect: namespace kmx::sling::drag::point skips "domain"
        src/kmx/sling/async/engine.cpp                  // Incorrect: namespace kmx::sling stops short of "async"
        ```
        *   **4.3.5.1** A file declares either the namespace of its directory, or a namespace one level deeper named exactly after the file, which makes the file itself the leaf of the hierarchy.
        *   **4.3.5.2** A `.cpp` file sits under `src/` at the same relative path as the header it implements and declares the same namespace.
        *   **4.3.5.3** Implementation namespaces follow the same rule: a `detail` or `internal` namespace either has its own `detail/` or `internal/` directory, or is nested inside the file that uses it. When nested inside a `.cpp` file, it must not define a class (see 3.4.6).
        *   **4.3.5.4** Include project headers by their full path from the source root (`#include <kmx/sling/domain/drag/point.hpp>`), never by a path relative to the including file.
        *   **4.3.5.5** **Exceptions:** Only the following are exempt:
            *   Declarations the language requires in a foreign namespace, such as `std::hash` specializations (see 4.4).
            *   User-defined literal operators, which are declared in the shared `kmx::literals` namespace next to the types they build, so that a single `using namespace kmx::literals;` brings every suffix into scope, as `std::literals` does.
    *   **4.3.6** **No `using namespace` in Headers:** Headers **must not** contain `using namespace` directives, at any scope. A directive in a header leaks into every file that includes it; qualify the names instead.

*   **4.4** **`std::hash` Specialization:** Provide a specialization of `std::hash` for any custom type intended to be used as a key in an unordered associative container.

*   **4.5** **Functions and Methods:**
    *   **4.5.1** Non-template methods and functions longer than 2 lines of code **must** be defined in `.cpp` files.
    *   **4.5.2** Prefer `static` functions in `.cpp` files rather than using anonymous namespaces for them.
    *   **4.5.3** Template methods longer than 12 lines of code **should** be defined outside classes.
    *   **4.5.4** `constexpr` and `consteval` functions and methods do not need the `inline` specifier.
    *   **4.5.5** **Branch Prediction Hints (C++20):** Use `[[likely]]`/`[[unlikely]]` only on hot-path branches whose outcome is strongly skewed and known (error paths, loop exits); leave balanced or unknown branches unannotated. They are hints only - correctness **must never** depend on them.
        *   **4.5.5.1** The attribute applies to the branch **statement**, and precedes a `case` label: `if (buffer.empty()) [[unlikely]] return error_t::no_data;`, `[[likely]] case state_t::running:`.
    *   **4.5.6** **Lambdas:** A lambda body longer than 3 lines **must** be extracted into a named function or method, leaving the lambda as glue that only forwards to it: `[this](const auto& item) { return process(item); }`.
    *   **4.5.7** **Function and Method Length:** A function or method body **must not** exceed one display page, approximately 24 lines of implementation code. If it exceeds this limit, split it into smaller functions or methods with clear, focused responsibilities.
    *   **4.5.8** **Parameter Count:** A function or method with 5 or more parameters **must** have its parameters packed into a `struct`, passed as a single argument. This improves call-site readability and avoids errors from misordered arguments of the same type.
        ```cpp
        namespace kmx::gui::widget
        {
            struct create_params
            {
                std::string_view name;
                std::uint32_t width {};
                std::uint32_t height {};
                color_t color {};
                bool visible {};
            };

            item create(const create_params& params); // Correct
            item create(std::string_view name, std::uint32_t width, std::uint32_t height, color_t color, bool visible); // Incorrect
        }
        ```
    *   **4.5.9** **`[[nodiscard]]`:** A function or method whose return value is its purpose **must** be marked `[[nodiscard]]`: every non-`void` `const` method, every side-effect-free computation, and every function returning `std::expected` or an error code. Ignoring such a result is almost always a bug, and the attribute lets the compiler report it.

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

*   **5.4** **File Preamble:** Every file **must** begin with a Doxygen preamble of three lines, in this order: `@file` with the path from the source root, `@brief` with a one-line summary of the file, and `@copyright`. In a header, the preamble precedes `#pragma once`.
    ```cpp
    /// @file api/kmx/sling/domain/drag/point.hpp
    /// @brief The samples a tabulated drag description is built from: coefficient bands and measured curve points.
    /// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
    #pragma once
    ```

---
Copyright © 2025 - present KMX Systems. All rights reserved.
