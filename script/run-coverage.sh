#!/usr/bin/env bash
set -euo pipefail

# Builds the project instrumented for gcov, runs the tests against it, and turns the result into a
# coverage report.
#
#   bash script/run-coverage.sh                  # unit tests, lcov report + HTML
#   bash script/run-coverage.sh --integration    # unit and integration tests
#   bash script/run-coverage.sh --gcov-only      # skip lcov; plain gcov listings only
#   bash script/run-coverage.sh --no-html        # lcov tracefile and summary, no genhtml run
#
# The instrumented build lives in output/coverage, apart from the ordinary output/debug tree: gcov writes
# its .gcda counters back into the build directory beside the .gcno files the compiler left there, so a
# coverage run and a normal build cannot share one tree without the counters following the wrong build.
#
# Outputs, all under output/coverage-report:
#   coverage.info    lcov tracefile, filtered down to the library sources (unfiltered coverpoints:
#                    the compiler-generated ones are dropped when the summary and HTML are produced)
#   html/index.html  browsable report (genhtml)
#   gcov/            per-file .gcov listings, when lcov is unavailable or --gcov-only was given

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

usage() {
    cat <<'USAGE'
usage: run-coverage.sh [options]

options:
  --integration   also run the integration tests, so their coverage is counted
  --gcov-only     produce plain gcov listings instead of an lcov report
  --no-html       stop after the lcov tracefile and summary
  --keep-data     keep .gcda counters from a previous run instead of clearing them
  -h, --help      show this message

environment:
  KMX_ENABLE_<FEATURE>   true/false, as for script/run-unit-tests.sh
  GCOV                   the gcov binary to use; must match the compiler that built the tree
USAGE
}

run_integration="false"
gcov_only="false"
generate_html="true"
keep_data="false"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --integration) run_integration="true" ;;
        --gcov-only) gcov_only="true" ;;
        --no-html) generate_html="false" ;;
        --keep-data) keep_data="true" ;;
        -h|--help) usage; exit 0 ;;
        *)
            echo "ERROR: unrecognized argument '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
    shift
done

export KMX_COVERAGE=1
export KMX_BUILD_ROOT="$repo_root/output/coverage"

source "$repo_root/script/feature/common.sh"

report_dir="$repo_root/output/coverage-report"
tracefile="$report_dir/coverage.info"

# gcov reads the .gcno the compiler wrote and the .gcda the run produced, and the two carry a format
# version stamp that has to match the compiler exactly. The system gcov is frequently older than the
# toolchain these builds use (GCC 16 out of /opt), and pairing them fails with a version error rather
# than a wrong number - so the gcov next to the profile's compiler is the one to use.
select_gcov_tool() {
    if [[ -n "${GCOV:-}" ]]; then
        echo "$GCOV"
        return 0
    fi

    local profile=""
    if [[ ${#qbs_profile_args[@]} -gt 0 ]]; then
        profile="${qbs_profile_args[0]#profile:}"
    fi

    if [[ -n "$profile" ]]; then
        local compiler
        compiler="$(qbs_profile_cxx_compiler "$profile")"
        if [[ -n "$compiler" ]]; then
            local candidate="$(dirname "$compiler")/gcov"
            if [[ -x "$candidate" ]]; then
                echo "$candidate"
                return 0
            fi
        fi
    fi

    command -v gcov || true
}

gcov_tool="$(select_gcov_tool)"
if [[ -z "$gcov_tool" ]]; then
    echo "ERROR: no gcov found. Install one, or point GCOV at the gcov matching your compiler." >&2
    exit 1
fi

echo "==> Coverage build tree: $KMX_BUILD_ROOT"
echo "==> gcov: $gcov_tool ($("$gcov_tool" --version | head -n 1))"
echo "==> qbs properties: ${qbs_instrumentation_args[*]}"

if [[ "$keep_data" == "false" && -d "$KMX_BUILD_ROOT" ]]; then
    # Counters accumulate across runs by design. That is useful when combining several runs on purpose
    # and misleading otherwise, so a run starts from zero unless asked not to.
    echo "==> Clearing counters from previous runs"
    find "$KMX_BUILD_ROOT" -name '*.gcda' -delete
fi

bash "$repo_root/script/run-unit-tests.sh"

if [[ "$run_integration" == "true" ]]; then
    bash "$repo_root/script/run-integration-tests.sh"
fi

if [[ -z "$(find "$KMX_BUILD_ROOT" -name '*.gcda' -print -quit 2>/dev/null)" ]]; then
    echo "ERROR: the run produced no .gcda counters under $KMX_BUILD_ROOT." >&2
    echo "       The tests ran against a binary that was not built for coverage." >&2
    exit 1
fi

mkdir -p "$report_dir"

report_with_gcov() {
    # The fallback, and what --gcov-only asks for: gcov alone, one .gcov listing per translation unit,
    # plus the per-file summary it prints as it goes.
    local gcov_dir="$report_dir/gcov"
    rm -rf "$gcov_dir"
    mkdir -p "$gcov_dir"

    echo "==> Writing gcov listings to $gcov_dir"
    (
        cd "$gcov_dir"
        find "$KMX_BUILD_ROOT" -name '*.gcda' -print0 |
            xargs -0 --no-run-if-empty "$gcov_tool" --branch-probabilities --preserve-paths
    ) > "$report_dir/gcov-summary.txt"

    # gcov reports on everything it was handed, the library sources and the Catch2 test sources alike.
    # Only the library's own lines answer the question a coverage run is asking.
    # One listing per translation unit, not one per file: a header included by several .cpp files is
    # reported once for each of them, each time counting only the lines that translation unit
    # instantiated. Merging those into a single figure per file is what lcov does and gcov does not.
    echo "==> Library source coverage (per translation unit; install lcov to merge these):"
    awk '
        /^File .*source\/library\// {
            file = $0
            sub(/^File .[^\x27]*source\/library\//, "", file)
            sub(/.$/, "", file)
            next
        }
        /^Lines executed:/ && file != "" {
            printf "    %-60s %s\n", file, $0
            file = ""
        }
    ' "$report_dir/gcov-summary.txt" | sort || true

    echo "==> Full listing: $report_dir/gcov-summary.txt"
}

if [[ "$gcov_only" == "true" ]]; then
    report_with_gcov
    exit 0
fi

if ! command -v lcov > /dev/null; then
    echo "==> lcov not found; falling back to plain gcov listings."
    echo "    Install it for the filtered tracefile and the HTML report:"
    echo "        sudo apt-get install lcov        # Debian/Ubuntu"
    echo "        sudo dnf install lcov            # Fedora/RHEL"
    report_with_gcov
    exit 0
fi

# lcov 2.x turns several conditions that 1.x merely warned about into errors, and every one of them is
# expected here: source files with no executable lines, objects compiled but never run by the selected
# tests, and counters left by a binary that has since been rebuilt.
lcov_ignore_args=()
lcov_filter_args=()
if [[ "$(lcov --version | grep -oE '[0-9]+' | head -n 1)" -ge 2 ]]; then
    lcov_ignore_args=(--ignore-errors mismatch,unused,empty,negative,source)

    # Most of what gcov calls a "branch" in this code is not a decision anyone wrote. A coroutine's
    # signature line alone carries fourteen of them - the ramp, the final-suspend edges, and an unwind
    # edge for every call that could throw - and none of them correspond to a condition a test could
    # take the other way. Left in, they dominate the number: 1048 branches of which 466 were artifacts,
    # reporting 66% where the code a person wrote was at 86%.
    #
    #   branch          drop branch counts on lines with no conditional in them
    #   no_exception_branch  drop the unwind edges gcov attaches to every call that can throw
    #   line            (brace,blank) drop hit counts on closing braces and blank lines
    #   trivial         drop compiler-generated bodies - defaulted constructors and the like
    #   function        merge the entries gcov emits per function *variant*. A destructor is compiled
    #                   twice - the ordinary one and the "deleting" one used by `delete p` on a base
    #                   pointer - and nothing in this library deletes through a base pointer, so the
    #                   second variant is emitted and never called. Both live at the same file and
    #                   line; merging them reports the destructor once, as covered.
    #   region,branch_region  honour the LCOV_EXCL_* markers in the sources. Each one sits on a line
    #                   that no test can reach - a std::terminate handler, a guard against a value the
    #                   type system already rules out, an arm of a race - with the reason beside it.
    #
    # Applied to the summary and to genhtml rather than to the tracefile: lcov --extract drops branch
    # records altogether when it filters, and coverage.info is more useful holding everything gcov saw.
    lcov_filter_args=(--filter branch,line,trivial,function,region,branch_region --rc no_exception_branch=1)
fi

echo "==> Capturing counters with lcov"
lcov --capture \
    --directory "$KMX_BUILD_ROOT" \
    --base-directory "$repo_root" \
    --gcov-tool "$gcov_tool" \
    --rc branch_coverage=1 \
    --output-file "$tracefile.all" \
    "${lcov_ignore_args[@]}" \
    --quiet

# What the report is about: the library. The test sources, the samples' main functions and the vendored
# dependencies under output/ would each dilute the number without saying anything about how well the
# library is exercised.
echo "==> Restricting the report to the library sources"
lcov --extract "$tracefile.all" \
    "$repo_root/source/library/*" \
    --rc branch_coverage=1 \
    --output-file "$tracefile" \
    "${lcov_ignore_args[@]}" \
    --quiet

rm -f "$tracefile.all"

echo "==> Summary"
lcov --summary "$tracefile" --rc branch_coverage=1 "${lcov_filter_args[@]}" "${lcov_ignore_args[@]}"

if [[ ${#lcov_filter_args[@]} -gt 0 ]]; then
    echo "    (branch counts exclude compiler-generated coverpoints - see lcov_filter_args above;"
    echo "     coverage.info itself keeps the unfiltered data)"
fi

if [[ "$generate_html" == "true" ]]; then
    if command -v genhtml > /dev/null; then
        echo "==> Generating HTML report"
        genhtml "$tracefile" \
            --output-directory "$report_dir/html" \
            --title "kmx-aio coverage" \
            --legend \
            --branch-coverage \
            "${lcov_filter_args[@]}" \
            "${lcov_ignore_args[@]}" \
            --quiet
        echo "==> HTML report: $report_dir/html/index.html"
    else
        echo "==> genhtml not found; tracefile written without an HTML report"
    fi
fi

echo "==> Tracefile: $tracefile"
echo "==> Coverage run completed"
