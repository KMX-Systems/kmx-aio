#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/../common.sh"

test_bin="$(find_test_bin)"
run_with_local_gcc_runtime timeout 60s "$test_bin" "[quic][readiness][integration][smoke][slow]"
run_with_local_gcc_runtime timeout 60s "$test_bin" "[quic][http3][readiness][integration][smoke][slow]"
