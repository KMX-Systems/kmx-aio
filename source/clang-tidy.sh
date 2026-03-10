#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-default}"
PROJECT_FILE="${PROJECT_FILE:-source.qbs}"
PROFILE="${PROFILE:-}"
JOBS="${JOBS:-$(nproc)}"
GCC_BIN="${GCC_BIN:-/usr/bin/g++}"

if ! command -v qbs >/dev/null 2>&1; then
    echo "error: qbs not found in PATH" >&2
    exit 1
fi

if ! command -v run-clang-tidy >/dev/null 2>&1; then
    echo "error: run-clang-tidy not found in PATH" >&2
    exit 1
fi

if [[ ! -x "$GCC_BIN" ]]; then
    echo "error: gcc compiler '$GCC_BIN' not found or not executable" >&2
    exit 1
fi

# Generate compilation database used by clang tooling.
if [[ -n "$PROFILE" ]]; then
    qbs generate -f "$PROJECT_FILE" -d "$BUILD_DIR" -g clangdb profile:"$PROFILE"
else
    qbs generate -f "$PROJECT_FILE" -d "$BUILD_DIR" -g clangdb
fi

COMPILE_DB="$BUILD_DIR/compile_commands.json"
if [[ ! -f "$COMPILE_DB" ]]; then
    echo "error: compilation database not found at '$COMPILE_DB'" >&2
    exit 1
fi

# clang-tidy 18 parses C++26 projects more reliably with the legacy spelling c++2c.
python3 - "$COMPILE_DB" << 'PY'
import json
import sys

path = sys.argv[1]
with open(path, 'r', encoding='utf-8') as f:
    db = json.load(f)

for entry in db:
    args = entry.get('arguments')
    if isinstance(args, list):
        entry['arguments'] = [('-std=c++2c' if a == '-std=c++26' else a) for a in args]
    cmd = entry.get('command')
    if isinstance(cmd, str):
        entry['command'] = cmd.replace('-std=c++26', '-std=c++2c')

with open(path, 'w', encoding='utf-8') as f:
    json.dump(db, f)
PY

# Force clang-tidy to use the same GCC toolchain family as the project build.
GCC_TOOLCHAIN="${GCC_TOOLCHAIN:-$($GCC_BIN -print-search-dirs | sed -n 's/^install: //p' | sed 's#/lib/gcc/.*##')}"

# Forward all extra CLI args to run-clang-tidy (for checks/header-filter/etc).
run-clang-tidy -p "$BUILD_DIR" -j "$JOBS" -extra-arg="--gcc-toolchain=$GCC_TOOLCHAIN" "$@"
