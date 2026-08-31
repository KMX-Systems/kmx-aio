#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../.." && pwd)"
source "$script_dir/../common.sh"

echo "==> Building SOME/IP-enabled unit tests"
(
	cd "$repo_root/source"
	qbs resolve -f source.qbs "${qbs_build_dir_args[@]}" "${qbs_profile_args[@]}" config:debug project.enable_someip:true "${qbs_instrumentation_args[@]}"
	qbs build -f source.qbs "${qbs_build_dir_args[@]}" "${qbs_profile_args[@]}" config:debug project.enable_someip:true "${qbs_instrumentation_args[@]}" -j"$(nproc)"
)

test_bin="$(find_test_bin)"
run_catch_tests timeout 90s "$test_bin" "[someip]~[integration]"
