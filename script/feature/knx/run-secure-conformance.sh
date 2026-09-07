#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../.." && pwd)"

source "$repo_root/script/feature/common.sh"

test_bin=""
vector_file="${KMX_KNX_SECURE_VECTOR_FILE:-$repo_root/documentation/features/knx/conformance/secure-profile-vectors.tsv}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --test-bin)
            test_bin="${2:-}"
            shift 2
            ;;
        --vector-file)
            vector_file="${2:-}"
            shift 2
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

strict_mode="$(normalize_bool "${KMX_KNX_STRICT_CONFORMANCE:-false}")"
[[ -n "$strict_mode" ]] || strict_mode="false"

if [[ -z "$test_bin" ]]; then
    test_bin="$(find_test_bin)"
fi

if [[ ! -f "$vector_file" ]]; then
    if [[ "$strict_mode" == "true" ]]; then
        echo "Secure conformance vector file missing: $vector_file" >&2
        exit 1
    fi
    echo "Skipping secure conformance run: vector file missing at $vector_file"
    exit 0
fi

export KMX_KNX_SECURE_VECTOR_FILE="$vector_file"
run_catch_tests timeout 25s "$test_bin" "[knx][secure][conformance]"
