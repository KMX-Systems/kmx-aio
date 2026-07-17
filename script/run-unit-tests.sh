#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/feature/common.sh"

auto_enable_features_from_test_binary_tags false

echo "==> Building unit tests with active features"
mapfile -t qbs_feature_args < <(build_qbs_feature_args)
(
    cd "$source_dir"
    qbs resolve -f source.qbs "${qbs_build_dir_args[@]}" config:debug "${qbs_feature_args[@]}"
    qbs build -f source.qbs "${qbs_build_dir_args[@]}" config:debug -j"$(nproc)" "${qbs_feature_args[@]}"
)

echo "==> Running feature-scoped unit tests"
for feature in "${feature_list[@]}"; do
    run_feature_script_if_enabled "$feature" "run-unit-tests.sh"
done

echo "==> Unit tests completed successfully"
