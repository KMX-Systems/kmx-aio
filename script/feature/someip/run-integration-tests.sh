#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../.." && pwd)"
source "$script_dir/../common.sh"

echo "==> Building SOME/IP-enabled integration tests"
(
	cd "$repo_root/source"
	qbs resolve -f source.qbs config:debug project.enable_someip:true
	qbs build -f source.qbs config:debug project.enable_someip:true -j"$(nproc)"
)

test_bin="$(find_test_bin)"
run_with_local_gcc_runtime timeout 90s "$test_bin" "[someip][integration]"
