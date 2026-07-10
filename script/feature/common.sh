#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
source_dir="$repo_root/source"

feature_list=(
    completion
    readiness
    http2
    http3
    openonload
    af_xdp
    spdk
    quic
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
    local enable_readiness_for_v4l2="false"
    local feature

    if is_feature_enabled "v4l2"; then
        enable_readiness_for_v4l2="true"
    fi

    for feature in "${feature_list[@]}"; do
        if [[ "$feature" == "v4l2" ]]; then
            continue
        fi

        if [[ "$feature" == "readiness" && "$enable_readiness_for_v4l2" == "true" ]]; then
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
    if [[ -d /opt/gcc-16/lib64 ]]; then
        LD_LIBRARY_PATH="/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-}" "$@"
    else
        "$@"
    fi
}

find_test_bin() {
    local bin
    bin="$(find "$source_dir/debug" -type f -name kmx-aio-test | head -n 1 || true)"
    if [[ -z "$bin" ]]; then
        echo "ERROR: kmx-aio-test binary not found in source/debug" >&2
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
