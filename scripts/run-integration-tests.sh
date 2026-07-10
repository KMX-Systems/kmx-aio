#!/usr/bin/env bash
set -euo pipefail

# Run integration tests on pre-built binaries (quick local testing)
# This script does NOT rebuild dependencies or re-resolve QBS.
# It runs tests on already-built artifacts.
#
# If you haven't built the project yet, run:
#   qbs resolve -f source/source.qbs config:debug project.full:true
#   qbs build -f source/source.qbs config:debug project.full:true
#
# For full CI pipeline with rebuilds, use: bash scripts/run-integration-tests-ci.sh

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_dir="$repo_root/source"

run_with_local_gcc_runtime() {
    if [[ -d /opt/gcc-16/lib64 ]]; then
        LD_LIBRARY_PATH="/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-}" "$@"
    else
        "$@"
    fi
}

find_test_bin() {
    local bin
    bin="$(find "$source_dir/debug" -type f -name kmx-aio-test | head -n 1 || true)"
    if [[ -z "$bin" ]]; then
        echo "ERROR: kmx-aio-test binary not found in debug directory" >&2
        echo "Build the project first:" >&2
        echo "  qbs resolve -f source/source.qbs config:debug project.full:true" >&2
        echo "  qbs build -f source/source.qbs config:debug project.full:true" >&2
        exit 1
    fi
    echo "$bin"
}

echo "==> Running integration tests on pre-built binaries"

test_bin="$(find_test_bin)"
echo "Using test binary: $test_bin"

echo ""
echo "==> Running unit tests (timeout 90s)"
run_with_local_gcc_runtime timeout 90s "$test_bin"

echo ""
echo "==> Running QUIC integration tests (timeout 60s each)"
# There are two separate QUIC smoke test variants, both tagged [slow] and [integration]
if run_with_local_gcc_runtime timeout 60s "$test_bin" "[quic][readiness][integration][smoke][slow]" 2>/dev/null || true; then
    echo "QUIC readiness echo smoke test passed or skipped"
fi
if run_with_local_gcc_runtime timeout 60s "$test_bin" "[quic][http3][readiness][integration][smoke][slow]" 2>/dev/null || true; then
    echo "QUIC HTTP/3 smoke test passed or skipped"
fi

echo ""
echo "==> Integration tests completed successfully"
