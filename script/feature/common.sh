#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
source_dir="$repo_root/source"

find_test_bin_path() {
    local -a search_roots=(
        "$repo_root/debug"
        "$repo_root/default"
        "$source_dir/debug"
        "$source_dir/default"
    )

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
        if [[ "$feature" == "v4l2" ]]; then
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

run_with_local_gcc_runtime() {
    local -a runtime_paths=()

    if [[ -d /opt/gcc-16/lib64 ]]; then
        runtime_paths+=("/opt/gcc-16/lib64")
    fi
    if [[ -d "$repo_root/build/spdk-local/install-local/lib" ]]; then
        runtime_paths+=("$repo_root/build/spdk-local/install-local/lib")
    fi
    if [[ -d "$repo_root/build/spdk-local/install-local/lib64" ]]; then
        runtime_paths+=("$repo_root/build/spdk-local/install-local/lib64")
    fi
    if [[ -d "$repo_root/build/someip/install-local/lib" ]]; then
        runtime_paths+=("$repo_root/build/someip/install-local/lib")
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
        echo "ERROR: kmx-aio-test binary not found in debug/default build outputs" >&2
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
