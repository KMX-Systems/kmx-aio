#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
knx_script_dir="$script_dir"
repo_root="$(cd "$script_dir/../../.." && pwd)"

build_and_test() {
    local build_root="$1"
    local readiness_enabled="$2"
    local label="$3"

    echo "==> Building KNX ${label} configuration"
    qbs resolve -d "$repo_root/$build_root" -f "$repo_root/source/source.qbs" config:debug \
        project.enable_knx:true \
        project.enable_readiness:"$readiness_enabled"
    qbs build -d "$repo_root/$build_root" -f "$repo_root/source/source.qbs" config:debug \
        project.enable_knx:true \
        project.enable_readiness:"$readiness_enabled"

    echo "==> Running KNX unit tests for ${label}"
    KMX_BUILD_ROOT="$repo_root/$build_root" bash "$knx_script_dir/run-unit-tests.sh"

    echo "==> Running KNX integration tests for ${label}"
    KMX_BUILD_ROOT="$repo_root/$build_root" bash "$knx_script_dir/run-integration-tests.sh"

    echo "==> Running KNX server and routing gates for ${label}"
    test_bin="$(find "$repo_root/$build_root/debug" -type f -name kmx-aio-test -not -path '*/install-root/*' -print -quit)"
    source "$repo_root/script/feature/common.sh"
    run_catch_tests timeout 25s "$test_bin" "[knx][server]"
    run_catch_tests timeout 25s "$test_bin" "[knx][routing]"
}

build_and_test "output/debug-knx-minimal" false "minimal"
build_and_test "output/debug-knx-readiness" true "readiness"

echo "==> KNX matrix tests completed successfully"