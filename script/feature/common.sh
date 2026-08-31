#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
source_dir="$repo_root/source"

# Everything generated lives under output/: the Qbs build trees (output/debug, output/default, ...) and,
# alongside them, the vendored dependency trees (lsquic, boringssl, spdk-local, open62541, someip) that
# bootstrap_optional_deps.sh builds and the .qbs files reference by relative path. Qt Creator is pointed at
# the same root, so no build artifact ever lands in the source tree. "qbs clean" only touches the Qbs build
# trees, so throwing a build away does not take hours of dependency builds with it - delete a specific
# output/<dep> directory when a dependency really needs rebuilding from scratch.
#
# KMX_BUILD_ROOT moves that root elsewhere, which is how the instrumented runners keep their trees
# apart: script/run-sanitizer-tests.sh builds under output/asan, script/run-coverage.sh under
# output/coverage. Mixing an instrumented and a plain build in one tree only wastes rebuilds, and
# leaves two binaries of the same name where the wrong one is easy to pick up.
qbs_build_root="${KMX_BUILD_ROOT:-$repo_root/output}"
qbs_build_dir_args=(-d "$qbs_build_root")

# Sets qbs_profile_args; see the comment at the top of that file for why the profile is not left to the
# machine-wide default.
source "$repo_root/script/qbs-profile.sh"

find_test_bin_path() {
    # output/ first, then the in-source trees a bare "qbs build" leaves behind - these scripts and the CI
    # workflow both run qbs from source_dir, so that is where an invocation without -d puts its artifacts.
    # Nothing looks at repo-root debug/ or default/ any more: those can only be leftovers from an older
    # layout, and picking a binary out of one silently runs a build nobody asked for.
    local -a search_roots=(
        "$qbs_build_root/debug"
        "$qbs_build_root/default"
        "$qbs_build_root/tsan"
    )

    # A build steered somewhere else by KMX_BUILD_ROOT - a sanitizer or coverage tree - has to be found
    # there and nowhere else. Falling back to the default output/ or to an in-source tree would hand
    # back a binary that runs perfectly well and carries none of the instrumentation that was asked for.
    if [[ -z "${KMX_BUILD_ROOT:-}" ]]; then
        search_roots+=(
            "$source_dir/debug"
            "$source_dir/default"
            "$source_dir/tsan"
        )
    fi

    local root bin
    for root in "${search_roots[@]}"; do
        if [[ -d "$root" ]]; then
            bin="$(find "$root" -type f -name kmx-aio-test | head -n 1 || true)"
            if [[ -n "$bin" ]]; then
                echo "$bin"
                return 0
            fi
        fi
    done

    return 1
}

feature_list=(
    core
    completion
    readiness
    http2
    http3
    openonload
    af_xdp
    spdk
    quic
    modbus
    avb
    opc_ua
    someip
    v4l2
    cuda
)

normalize_bool() {
    local value="${1:-}"
    value="$(echo "$value" | tr '[:upper:]' '[:lower:]')"
    case "$value" in
        1|true|yes|on) echo "true" ;;
        0|false|no|off) echo "false" ;;
        *) echo "" ;;
    esac
}

feature_default_enabled() {
    local feature="$1"
    case "$feature" in
        core) echo "true" ;;
        completion) echo "true" ;;
        *) echo "false" ;;
    esac
}

feature_env_name() {
    local feature="$1"
    local upper
    upper="$(echo "$feature" | tr '[:lower:]' '[:upper:]')"
    echo "KMX_ENABLE_${upper}"
}

is_feature_enabled() {
    local feature="$1"
    local env_name
    env_name="$(feature_env_name "$feature")"

    local env_value="${!env_name:-}"
    local normalized
    normalized="$(normalize_bool "$env_value")"
    if [[ -n "$normalized" ]]; then
        [[ "$normalized" == "true" ]]
        return
    fi

    [[ "$(feature_default_enabled "$feature")" == "true" ]]
}

build_qbs_feature_args() {
    local args=()
    local enable_readiness_dependency="false"
    local feature

    if is_feature_enabled "v4l2" || is_feature_enabled "modbus"; then
        enable_readiness_dependency="true"
    fi

    for feature in "${feature_list[@]}"; do
        # Neither of these names a project.enable_* property: core is the always-built library core, and
        # v4l2 rides on whichever executor backend is already enabled.
        if [[ "$feature" == "core" || "$feature" == "v4l2" ]]; then
            continue
        fi

        if [[ "$feature" == "readiness" && "$enable_readiness_dependency" == "true" ]]; then
            args+=("project.enable_readiness:true")
            continue
        fi

        if is_feature_enabled "$feature"; then
            args+=("project.enable_${feature}:true")
        else
            args+=("project.enable_${feature}:false")
        fi
    done
    printf '%s\n' "${args[@]}"
}

# The qbs properties that turn instrumentation on, from the KMX_SANITIZERS / KMX_COVERAGE that
# script/run-sanitizer-tests.sh and script/run-coverage.sh export.
#
# Every qbs invocation these scripts make has to carry them, not just the first: a feature script that
# rebuilds the tree with its own feature flag and no instrumentation property would resolve the project
# without the sanitizer and quietly replace the instrumented binary with a plain one. The properties are
# always spelled out, true and false alike, so that a tree previously built with a sanitizer is switched
# back off rather than inheriting the setting from its build graph.
build_qbs_instrumentation_args() {
    local sanitizers="${KMX_SANITIZERS:-}"
    local -a args=()

    args+=("project.enable_asan:$(instrumentation_bool "$sanitizers" asan)")
    args+=("project.enable_ubsan:$(instrumentation_bool "$sanitizers" ubsan)")
    args+=("project.enable_tsan:$(instrumentation_bool "$sanitizers" tsan)")

    if [[ "$(normalize_bool "${KMX_COVERAGE:-}")" == "true" ]]; then
        args+=("project.enable_coverage:true")
    else
        args+=("project.enable_coverage:false")
    fi

    # The branches that handle a failing system call cannot be reached by calling the public API, so a
    # coverage build compiles the syscall seam's faulting policy in and the tests drive them directly.
    # KMX_FAULT_INJECTION overrides, for measuring a build without the seam.
    local fault_injection
    fault_injection="$(normalize_bool "${KMX_FAULT_INJECTION:-}")"
    if [[ -z "$fault_injection" ]]; then
        fault_injection="$(normalize_bool "${KMX_COVERAGE:-}")"
        [[ -n "$fault_injection" ]] || fault_injection="false"
    fi
    args+=("project.enable_fault_injection:$fault_injection")

    printf '%s\n' "${args[@]}"
}

instrumentation_bool() {
    local selection="$1"
    local name="$2"

    # Matched on word boundaries: "asan" must not be found inside "ubsan".
    if [[ ",${selection//+/,}," == *",${name},"* ]]; then
        echo "true"
    else
        echo "false"
    fi
}

# The runtime half of a sanitizer build. The compiler decides what is checked; these decide what happens
# when a check fires, and without them a UBSan finding prints one line and lets the process carry on to
# exit 0 - a test run that stays green while reporting undefined behaviour.
#
# Each variable is only filled in when the caller has not set it, so an investigation can still ask for
# something else (a leak hunt with detect_leaks=0 turned back on, say) from the environment.
apply_sanitizer_runtime_options() {
    local sanitizers="${KMX_SANITIZERS:-}"
    [[ -n "$sanitizers" ]] || return 0

    local suppressions_dir="$repo_root/script/sanitizer"

    if [[ "$(instrumentation_bool "$sanitizers" asan)" == "true" && -z "${ASAN_OPTIONS:-}" ]]; then
        # detect_leaks is on by default on Linux; naming it here is what makes the suppression file
        # below reachable, since LeakSanitizer only reads LSAN_OPTIONS when it is actually running.
        export ASAN_OPTIONS="detect_leaks=1:detect_stack_use_after_return=1:strict_string_checks=1:check_initialization_order=1:print_stacktrace=1"
    fi

    if [[ "$(instrumentation_bool "$sanitizers" asan)" == "true" && -z "${LSAN_OPTIONS:-}" &&
          -f "$suppressions_dir/lsan.supp" ]]; then
        export LSAN_OPTIONS="suppressions=$suppressions_dir/lsan.supp:print_suppressions=0"
    fi

    if [[ "$(instrumentation_bool "$sanitizers" ubsan)" == "true" && -z "${UBSAN_OPTIONS:-}" ]]; then
        # halt_on_error is what turns a finding into a failed test run: UBSan otherwise recovers from
        # every check and the process still exits 0.
        local ubsan_options="print_stacktrace=1:halt_on_error=1"
        if [[ -f "$suppressions_dir/ubsan.supp" ]]; then
            ubsan_options="$ubsan_options:suppressions=$suppressions_dir/ubsan.supp"
        fi
        export UBSAN_OPTIONS="$ubsan_options"
    fi

    if [[ "$(instrumentation_bool "$sanitizers" tsan)" == "true" && -z "${TSAN_OPTIONS:-}" ]]; then
        local tsan_options="halt_on_error=1:second_deadlock_stack=1"
        if [[ -f "$suppressions_dir/tsan.supp" ]]; then
            tsan_options="$tsan_options:suppressions=$suppressions_dir/tsan.supp"
        fi
        export TSAN_OPTIONS="$tsan_options"
    fi
}

run_with_local_gcc_runtime() {
    local -a runtime_paths=()

    # An instrumented binary needs its runtime options in place before it starts, and every test binary
    # these scripts launch is launched from here.
    apply_sanitizer_runtime_options

    if [[ -d /opt/gcc-16/lib64 ]]; then
        runtime_paths+=("/opt/gcc-16/lib64")
    fi
    if [[ -d "$repo_root/output/spdk-local/install-local/lib" ]]; then
        runtime_paths+=("$repo_root/output/spdk-local/install-local/lib")
    fi
    if [[ -d "$repo_root/output/spdk-local/install-local/lib64" ]]; then
        runtime_paths+=("$repo_root/output/spdk-local/install-local/lib64")
    fi
    if [[ -d "$repo_root/output/someip/install-local/lib" ]]; then
        runtime_paths+=("$repo_root/output/someip/install-local/lib")
    fi

    local path_prefix=""
    if [[ ${#runtime_paths[@]} -gt 0 ]]; then
        path_prefix="$(IFS=:; echo "${runtime_paths[*]}")"
    fi

    if [[ -n "$path_prefix" ]]; then
        LD_LIBRARY_PATH="$path_prefix:${LD_LIBRARY_PATH:-}" "$@"
    else
        "$@"
    fi
}

# Set once, here, next to qbs_build_dir_args and qbs_profile_args: every script that drives qbs can then
# pass "${qbs_instrumentation_args[@]}" without deciding anything for itself.
mapfile -t qbs_instrumentation_args < <(build_qbs_instrumentation_args)

# Runs the Catch2 test binary through the local runtime paths.
#
# Catch2 exits non-zero both when a filter matches nothing and when every test it selected was skipped.
# Neither is a failure here: a filter matches nothing when the tests it names were not compiled into this
# configuration, and a test skips when it has looked for its prerequisites - a sample binary, a device, a
# certificate - and not found them. Only real assertion failures should stop the run, so pass Catch2 the
# flag that says so rather than having every feature script decide for itself.
run_catch_tests() {
    run_with_local_gcc_runtime "$@" --allow-running-no-tests
}

feature_tag_name() {
    local feature="$1"
    case "$feature" in
        af_xdp) echo "xdp" ;;
        cuda) echo "gpu" ;;
        *) echo "$feature" ;;
    esac
}

auto_enable_features_from_test_binary_tags() {
    # $1: "true" (default) requires both [integration] and the feature tag on the
    #     same test case (used by the integration runner, which only cares about
    #     features with integration coverage). "false" matches the feature tag
    #     alone, which is appropriate for the unit-test runner (a feature may be
    #     built and unit-tested without having any [integration]-tagged cases).
    local require_integration="${1:-true}"

    local test_bin
    test_bin="$(find_test_bin_path || true)"
    if [[ -z "$test_bin" ]]; then
        echo "==> No kmx-aio-test binary found; using KMX_ENABLE_* / defaults"
        return 0
    fi

    local tests_output
    tests_output="$(run_with_local_gcc_runtime "$test_bin" --list-tests --verbosity high 2>/dev/null || true)"
    if [[ -z "$tests_output" ]]; then
        echo "==> Could not read test list from $test_bin; using KMX_ENABLE_* / defaults"
        return 0
    fi

    local feature env_name tag pattern
    for feature in "${feature_list[@]}"; do
        if [[ "$feature" == "core" ]]; then
            continue
        fi

        env_name="$(feature_env_name "$feature")"
        if [[ -n "${!env_name:-}" ]]; then
            continue
        fi

        tag="$(feature_tag_name "$feature")"
        if [[ "$require_integration" == "true" ]]; then
            pattern="\[integration\].*\[$tag\]|\[$tag\].*\[integration\]"
        else
            pattern="\[$tag\]"
        fi

        if grep -Eq "$pattern" <<< "$tests_output"; then
            export "$env_name=true"
        else
            export "$env_name=false"
        fi
    done

    echo "==> Feature auto-detection from test binary tags completed"
}

find_test_bin() {
    local bin
    bin="$(find_test_bin_path || true)"
    if [[ -z "$bin" ]]; then
        echo "ERROR: kmx-aio-test binary not found in output/debug, output/default, or output/tsan outputs" >&2
        exit 1
    fi
    echo "$bin"
}

run_feature_script_if_enabled() {
    local feature="$1"
    local script_name="$2"

    if ! is_feature_enabled "$feature"; then
        echo "==> [${feature}] disabled, skipping ${script_name}"
        return 0
    fi

    local feature_script="$repo_root/script/feature/${feature}/${script_name}"
    if [[ ! -f "$feature_script" ]]; then
        echo "ERROR: missing feature script $feature_script" >&2
        exit 1
    fi

    echo "==> [${feature}] running ${script_name}"
    bash "$feature_script"
}
