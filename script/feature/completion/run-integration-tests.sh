#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/../common.sh"

test_bin="$(find_test_bin)"
run_with_local_gcc_runtime timeout 90s "$test_bin" "[completion][integration]"
