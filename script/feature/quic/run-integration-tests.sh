#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/../common.sh"

test_bin="$(find_test_bin)"

# Which of these are built depends on the other feature gates - the smoke tests need the readiness and http3
# products - so a filter matching nothing means "not built in this configuration", not a failure.
run_catch_tests timeout 60s "$test_bin" "[quic][transport][integration]"
run_catch_tests timeout 60s "$test_bin" "[quic][readiness][integration][smoke][slow]"
run_catch_tests timeout 60s "$test_bin" "[quic][http3][readiness][integration][smoke][slow]"
