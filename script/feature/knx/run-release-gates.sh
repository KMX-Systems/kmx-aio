#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
knx_script_dir="$script_dir"
repo_root="$(cd "$script_dir/../../.." && pwd)"
build_root="${KMX_BUILD_ROOT:-$repo_root/output/knx-release-gates}"
# qbs runs from source/ below, so a relative root would land under source/ rather than under output/.
[[ "$build_root" = /* ]] || build_root="$repo_root/$build_root"
# The profile every other build script uses. Without it qbs falls back to the machine-wide defaultProfile,
# which names whatever toolchain happened to be the default when it was last set.
source "$repo_root/script/qbs-profile.sh"

cd "$repo_root/source"
qbs resolve -d "$build_root" -f source.qbs "${qbs_profile_args[@]}" config:debug \
    project.enable_knx:true \
    project.enable_readiness:true project.enable_completion:true
qbs build -d "$build_root" -f source.qbs "${qbs_profile_args[@]}" config:debug \
    project.enable_knx:true \
    project.enable_readiness:true project.enable_completion:true --products kmx-aio-test

cd "$repo_root"
find_test_bin() {
    find "$build_root/debug" -type f -name kmx-aio-test -not -path '*/install-root/*' -print -quit
}

bin="$(find_test_bin)"
# A stale build graph lets qbs report success with nothing built; a release gate that ran no binary proves nothing.
if [[ -z "$bin" || ! -x "$bin" ]]; then
    echo "No kmx-aio-test was built under $build_root." >&2
    exit 1
fi
source "$repo_root/script/feature/common.sh"
bash "$knx_script_dir/run-vendor-interoperability.sh" --validate
run_catch_tests timeout 25s "$bin" "[knx]"
run_catch_tests timeout 25s "$bin" "[knx][server]"
run_catch_tests timeout 25s "$bin" "[knx][routing]"
# The KNX Secure suites: the keyring, IP Secure routing and tunnelling, KNXnet/IP over TCP, and Data Secure. Their loopback
# cases wait on real connections, hence the longer limit.
run_catch_tests timeout 300s "$bin" "[knx][keyring]"
run_catch_tests timeout 300s "$bin" "[knx][secure]"
run_catch_tests timeout 300s "$bin" "[knx][tcp]"
run_catch_tests timeout 300s "$bin" "[knx][data_secure]"
run_catch_tests timeout 25s "$bin"
bash "$knx_script_dir/interoperability-matrix.sh" verify-release
bash "$knx_script_dir/interoperability-matrix.sh" render
git diff --check
bash -n "$knx_script_dir/run-matrix-tests.sh" "$knx_script_dir/run-release-gates.sh" "$knx_script_dir/interoperability-matrix.sh" \
    "$knx_script_dir/run-vendor-interoperability.sh" "$knx_script_dir/run-secure-suites.sh" "$knx_script_dir/run-fuzz.sh"

echo "KNX release gates passed. Interoperability evidence rendered to documentation/features/knx/interoperability/matrix.md."
