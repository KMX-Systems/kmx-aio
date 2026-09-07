#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../.." && pwd)"
results_file="${KMX_KNX_VENDOR_RESULTS_FILE:-$repo_root/documentation/features/knx/interoperability/vendor-results.tsv}"
mode="validate"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --results-file)
            results_file="${2:-}"
            shift 2
            ;;
        --apply)
            mode="apply"
            shift
            ;;
        --validate)
            mode="validate"
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

if [[ ! -f "$results_file" ]]; then
    echo "Vendor interoperability results file is missing: $results_file" >&2
    exit 1
fi

if [[ "$mode" == "apply" ]]; then
    bash "$script_dir/interoperability-matrix.sh" import --file "$results_file" --require-capture-files
else
    bash "$script_dir/interoperability-matrix.sh" import --file "$results_file" --require-capture-files --dry-run
fi
