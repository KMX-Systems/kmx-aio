#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/feature/common.sh"

echo "==> Running feature-scoped integration tests on pre-built binaries"
for feature in "${feature_list[@]}"; do
    run_feature_script_if_enabled "$feature" "run-integration-tests.sh"
done

echo "==> Integration tests completed successfully"
