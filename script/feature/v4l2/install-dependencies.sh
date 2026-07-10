#!/usr/bin/env bash
set -euo pipefail

if ! command -v apt-get >/dev/null 2>&1; then
    echo "This script currently supports Ubuntu/Debian only (apt-get required)." >&2
    exit 1
fi

if [[ "${EUID}" -ne 0 ]]; then
    SUDO="sudo"
else
    SUDO=""
fi

echo "[v4l2] Installing optional V4L2 tooling (Ubuntu/Debian)..."
${SUDO} apt-get update
${SUDO} apt-get install -y v4l-utils

echo "[v4l2] Verifying tools..."
command -v v4l2-ctl >/dev/null 2>&1

echo "[v4l2] Done."