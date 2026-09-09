#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
knx_script_dir="$script_dir"
repo_root="$(cd "$script_dir/../../.." && pwd)"
build_root="${KMX_BUILD_ROOT:-$repo_root/output/knx-release-gates}"

cd "$repo_root/source"
qbs resolve -d "$build_root" -f source.qbs config:debug \
    project.enable_knx:true \
    project.enable_readiness:true project.enable_completion:true
qbs build -d "$build_root" -f source.qbs config:debug \
    project.enable_knx:true \
    project.enable_readiness:true project.enable_completion:true --products kmx-aio-test

cd "$repo_root"
find_test_bin() {
    find "$build_root/debug" -type f -name kmx-aio-test -not -path '*/install-root/*' -print -quit
}

bin="$(find_test_bin)"
[[ -n "$bin" ]]
source "$repo_root/script/feature/common.sh"
KMX_KNX_STRICT_CONFORMANCE=true bash "$knx_script_dir/run-secure-conformance.sh" --test-bin "$bin"
KMX_KNX_STRICT_CONFORMANCE=true bash "$knx_script_dir/run-keyring-conformance.sh" --test-bin "$bin"
bash "$knx_script_dir/run-vendor-interoperability.sh" --validate
run_catch_tests timeout 25s "$bin" "[knx]"
run_catch_tests timeout 25s "$bin" "[knx][server]"
run_catch_tests timeout 25s "$bin" "[knx][routing]"
run_catch_tests timeout 25s "$bin"
bash "$knx_script_dir/interoperability-matrix.sh" verify
bash "$knx_script_dir/interoperability-matrix.sh" render
git diff --check
bash -n "$knx_script_dir/run-matrix-tests.sh" "$knx_script_dir/run-release-gates.sh" "$knx_script_dir/interoperability-matrix.sh" "$knx_script_dir/run-secure-conformance.sh" "$knx_script_dir/run-keyring-conformance.sh" "$knx_script_dir/run-vendor-interoperability.sh"

echo "KNX release gates passed. Interoperability evidence rendered to documentation/features/knx/interoperability/matrix.md."
