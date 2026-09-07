#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../.." && pwd)"

source "$repo_root/script/feature/common.sh"

test_bin=""
conformance_dir="${KMX_KNX_KEYRING_CONFORMANCE_DIR:-$repo_root/documentation/features/knx/conformance/keyrings}"
cases_file="${KMX_KNX_KEYRING_CASES_FILE:-$conformance_dir/cases.tsv}"
password_file="${KMX_KNX_KEYRING_PASSWORDS_FILE:-$conformance_dir/passwords.tsv}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --test-bin)
            test_bin="${2:-}"
            shift 2
            ;;
        --conformance-dir)
            conformance_dir="${2:-}"
            shift 2
            ;;
        --cases-file)
            cases_file="${2:-}"
            shift 2
            ;;
        --passwords-file)
            password_file="${2:-}"
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

if [[ ! -d "$conformance_dir" || ! -f "$cases_file" || ! -f "$password_file" ]]; then
    if [[ "$strict_mode" == "true" ]]; then
        echo "Keyring conformance inputs missing" >&2
        echo "  dir: $conformance_dir" >&2
        echo "  cases: $cases_file" >&2
        echo "  passwords: $password_file" >&2
        exit 1
    fi
    echo "Skipping keyring conformance run: expected conformance inputs are missing"
    exit 0
fi

export KMX_KNX_KEYRING_CONFORMANCE_DIR="$conformance_dir"
export KMX_KNX_KEYRING_CASES_FILE="$cases_file"
export KMX_KNX_KEYRING_PASSWORDS_FILE="$password_file"
run_catch_tests timeout 25s "$test_bin" "[knx][keyring][conformance]"
