#!/usr/bin/env bash
set -euo pipefail

# Run all unit tests with all available features enabled

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_dir="$repo_root/source"

echo "==> Building unit tests with all features enabled"
(
    cd "$source_dir"
    qbs resolve -f source.qbs config:debug \
        project.enable_readiness:true \
        project.enable_quic:true \
        project.enable_http3:true

    qbs build -f source.qbs config:debug -j"$(nproc)" \
        project.enable_readiness:true \
        project.enable_quic:true \
        project.enable_http3:true
)

echo "==> Running unit tests"

# Find test binary
TEST_BIN="$(find "$source_dir/debug" -type f -name kmx-aio-test | head -n 1 || true)"
if [[ -z "$TEST_BIN" ]]; then
    echo "kmx-aio-test binary not found" >&2
    exit 1
fi

# Run with gcc-16 runtime if available
if [[ -d /opt/gcc-16/lib64 ]]; then
    LD_LIBRARY_PATH="/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-}" timeout 180s "$TEST_BIN"
else
    timeout 180s "$TEST_BIN"
fi

echo "==> Unit tests completed successfully"
